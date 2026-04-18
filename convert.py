#!/usr/bin/env python3
"""
Convert PDP-10-AS (MACRO-10) assembly source to ASL assembler format.

This script reads a pre-stripped assembly listing file and writes an
ASL-compatible assembly file, applying eleven conversion passes:

  Pass 0: Strip END block (remove lines from END to next SEARCH)
  Pass 1: Numeric constants (^O octal -> hex, ^D decimal -> plain decimal)
  Pass 2: Macro definitions (DEFINE -> MACRO/ENDM)
  Pass 3: Variable definitions (= and == -> EQU)
  Pass 4: Conditional assembly (IFE/IFN/IF1 -> IF/ENDIF)
  Pass 5: String constants (DC"string" -> DB 'string', ACRLF -> DB 13, 10)
  Pass 6: Comment blocks (COMMENT x...x -> ;-prefixed lines)
  Pass 7: PDP-10 directives (PAGE, SUBTTL, TITLE, RADIX, SEARCH -> commented)
  Pass 8: $CODE symbol (replace $CODE with CODE, insert CODE EQU 0)
  Pass 9: Dollar-sign symbols (CHR$ -> CHR_S, MID$ -> MID_S, etc.)
  Pass 10: Directive rewrites (bare number -> DB, BLOCK -> DS, RELOC -> ORG)
"""

import sys
import re


# ---------------------------------------------------------------------------
# Warning infrastructure
# ---------------------------------------------------------------------------

class Warnings:
    """Collects warnings with original line numbers and actionable messages."""

    def __init__(self, filename):
        self.filename = filename
        self.messages = []

    def warn(self, lineno, category, message, fix_hint):
        """Record a warning.

        lineno:    1-based line number in the original input file.
        category:  short tag like "OCR" or "SYNTAX".
        message:   what was detected.
        fix_hint:  what the user should do to fix the input file.
        """
        self.messages.append((lineno, category, message, fix_hint))

    def print_all(self):
        """Print all collected warnings to stderr, sorted by line number."""
        for lineno, category, message, fix_hint in sorted(self.messages):
            loc = f"{self.filename}:{lineno}"
            print(f"WARNING [{category}] {loc}: {message}", file=sys.stderr)
            print(f"  FIX: {fix_hint}", file=sys.stderr)

    @property
    def count(self):
        return len(self.messages)


# Each pass works with a list of (lineno, text) tuples so that warnings
# can reference the original source line number.


# ---------------------------------------------------------------------------
# Pass 0: Strip END Block
# ---------------------------------------------------------------------------

def pass0_strip_end_block(numbered_lines, warnings):
    """Remove lines from a bare END directive to the next SEARCH directive.

    The assembler listing includes a symbol table / linker output block
    between END and the start of the next source module (SEARCH).  These
    lines are not assembly source and must be stripped.
    """
    result = []
    skipping = False
    for lineno, line in numbered_lines:
        if not skipping:
            if re.match(r'^END\s*$', line):
                skipping = True
                continue
            result.append((lineno, line))
        else:
            if re.match(r'^SEARCH\b', line):
                skipping = False
                result.append((lineno, line))

    # Rewrite DC(X) -> DC X before any other passes (needed inside macro bodies)
    result2 = []
    for lineno, line in result:
        result2.append((lineno, re.sub(r'\bDC\(([^)]+)\)', r'DC \1', line)))
    return result2


# ---------------------------------------------------------------------------
# Pass 1: Numeric Constants
# ---------------------------------------------------------------------------

def pass1_numeric_constants(numbered_lines, warnings):
    """Convert ^O and ^D numeric constants, and rebase unadorned integers.

    Tracks the current RADIX (default 8 for MACRO-10).  When the radix
    is not 10, bare digit sequences that are valid in the current radix
    are converted to decimal.  Numbers with ^O, ^D, or trailing H are
    handled separately and are not affected by the radix.
    """
    radix = 8  # MACRO-10 default radix is octal
    result = []
    for lineno, line in numbered_lines:
        # Check for RADIX directive (always base-10 argument)
        m_radix = re.match(r'^\s*RADIX\s+(\d+)', line)
        if m_radix:
            radix = int(m_radix.group(1))

        # Convert unadorned integers BEFORE stripping ^O/^D prefixes,
        # so that ^D14 still has the ^ prefix and won't match as bare "14".
        new_line = line
        if radix != 10:
            new_line = _convert_unadorned_integers(new_line, radix)

        # Convert explicit ^O and ^D prefixed constants
        new_line = re.sub(r'\^O([0-7]+)', _convert_octal, new_line)
        new_line = re.sub(r'\^D([0-9]+)', _convert_decimal, new_line)
        # Check for ^D followed by non-digits (possible OCR issue)
        m_bad_d = re.search(r'\^D([^0-9\s,;<>)\]])', new_line)
        if m_bad_d:
            warnings.warn(lineno, "OCR",
                f"'^D' followed by non-digit '{m_bad_d.group(1)}': ...{m_bad_d.group(0)}...",
                f"Check that ^D is followed by a valid decimal number on this line.")

        result.append((lineno, new_line))
    return result


def _convert_octal(m):
    """Convert a ^O octal constant to hex format XXXXH."""
    value = int(m.group(1), 8)
    hex_str = format(value, 'X')
    if hex_str[0] in 'ABCDEF':
        hex_str = '0' + hex_str
    return hex_str + 'H'


def _convert_decimal(m):
    """Convert a ^D decimal constant by stripping the ^D prefix."""
    return m.group(1)


def _convert_unadorned_integers(line, radix):
    """Convert bare integers in a line from the given radix to decimal.

    Skips numbers that:
    - Are followed by 'H' (hex literals)
    - Are preceded by '^O' or '^D' (already handled)
    - Are part of an identifier (preceded by a letter/underscore/dot)
    - Contain digits >= radix (not valid in the current radix)
    """
    # Skip comment-only lines
    stripped = line.lstrip()
    if stripped.startswith(';') or stripped.startswith('COMMENT'):
        return line

    # Split line into code and comment at first unquoted semicolon
    code_end = len(line)
    in_single = False
    in_double = False
    for idx, ch in enumerate(line):
        if ch == "'" and not in_double:
            in_single = not in_single
        elif ch == '"' and not in_single:
            in_double = not in_double
        elif ch == ';' and not in_single and not in_double:
            code_end = idx
            break

    code_part = line[:code_end]
    comment_part = line[code_end:]

    # Replace bare integers in the code portion only
    def _rebase(m):
        num_str = m.group(0)
        # Check if any digit is >= radix (not a valid number in this radix)
        for d in num_str:
            if int(d) >= radix:
                return num_str
        value = int(num_str, radix)
        return str(value)

    # Match bare digit sequences:
    # - not preceded by a letter, digit, underscore, dot, or ^ (not part of identifier/prefix)
    # - not followed by a letter or underscore (not part of identifier or hex suffix)
    result = re.sub(r'(?<![A-Za-z0-9_.^])(\d+)(?![A-Za-z_])', _rebase, code_part)

    return result + comment_part


# ---------------------------------------------------------------------------
# Pass 2: Macro Definitions
# ---------------------------------------------------------------------------

FIXED_INDENT = '        '   # 8 spaces used for macro body / IF / ENDIF


def _replace_angle_brackets(text):
    """Replace < with ( and > with ) but preserve the <> pair."""
    text = text.replace('<>', '\x00')
    text = text.replace('<', '(').replace('>', ')')
    text = text.replace('\x00', '<>')
    return text


def _indent_macro_body(text, indent):
    """Re-indent a macro body line with a single fixed indent level.

    Labels (identifier followed by ':') are placed at the macro's own
    indent level; EQU lines start at column 0; all other non-blank
    lines get one fixed indent.
    """
    stripped = text.lstrip()
    if not stripped:
        return text
    if re.match(r'^[.A-Za-z_]\w*:', stripped):
        return indent + stripped
    if re.match(r'^[.A-Za-z_]\w*\s+EQU\b', stripped):
        return stripped
    return indent + FIXED_INDENT + stripped


def pass2_macro_definitions(numbered_lines, warnings):
    """Convert DEFINE name,<body> to ASL MACRO/ENDM style."""
    result = []
    i = 0
    while i < len(numbered_lines):
        lineno, line = numbered_lines[i]

        m = re.match(
            r'^(\s*)DEFINE\s+(\w+)\s*(\([^)]*\))?\s*,\s*<(.*)$',
            line
        )
        if not m:
            result.append((lineno, line))
            i += 1
            continue

        start_lineno = lineno
        indent = m.group(1)
        name = m.group(2)
        params = m.group(3)
        rest = m.group(4)

        if params:
            param_list = params[1:-1].strip()
            header = f"{indent}{name} MACRO {param_list}"
        else:
            header = f"{indent}{name} MACRO"
        result.append((lineno, header))

        depth = 1
        body_lines = []     # list of (lineno, text)
        current = rest
        current_lineno = lineno
        while True:
            j = 0
            line_content = ""
            while j < len(current):
                ch = current[j]
                if ch == '<':
                    depth += 1
                    line_content += ch
                elif ch == '>':
                    depth -= 1
                    if depth == 0:
                        if line_content.strip() or body_lines:
                            line_content = _indent_macro_body(
                                line_content, indent)
                            body_lines.append((current_lineno, line_content))
                        break
                    else:
                        line_content += ch
                else:
                    line_content += ch
                j += 1

            if depth == 0:
                break
            else:
                # Check if next line starts a new DEFINE (missing > from OCR)
                peek_i = i + 1
                if peek_i < len(numbered_lines) and re.match(
                    r'^\s*DEFINE\s+\w+', numbered_lines[peek_i][1]
                ):
                    current = _indent_macro_body(
                        current, indent)
                    body_lines.append((current_lineno, current))
                    warnings.warn(start_lineno, "OCR",
                        f"Macro '{name}' has no closing '>'. "
                        f"Next DEFINE found at line {numbered_lines[peek_i][0]}.",
                        f"Add '>' at end of line {current_lineno} to close "
                        f"the macro body. E.g.: {current.rstrip()}>")
                    break

                current = _indent_macro_body(
                    current, indent)
                body_lines.append((current_lineno, current))
                i += 1
                if i >= len(numbered_lines):
                    warnings.warn(start_lineno, "OCR",
                        f"Macro '{name}' is never closed (reached end of file).",
                        f"Add '>' to close the macro body somewhere after "
                        f"line {start_lineno}.")
                    break
                current_lineno, current = numbered_lines[i]

        for bl in body_lines:
            result.append(bl)
        result.append((current_lineno, f"{indent}        ENDM"))

        i += 1
    return result


# ---------------------------------------------------------------------------
# Pass 3: Variable Definitions (EQU)
# ---------------------------------------------------------------------------

def pass3_equ_conversion(numbered_lines, warnings):
    """Convert symbol= and symbol== assignments to EQU.

    In PDP-10-AS (MACRO-10), both = and == are direct assignments
    (redefinable symbols). The only difference is that == suppresses the
    symbol from debugger output while = leaves it visible. This distinction
    has no equivalent in ASL, so both map to EQU.
    """
    result = []
    pattern = re.compile(
        r'^(\s*)'
        r'([.A-Za-z_]\w*)'
        r'(==?:?)'
        r'(.*)$'
    )
    for lineno, line in numbered_lines:
        m = pattern.match(line)
        if m:
            indent = m.group(1)
            symbol = m.group(2)
            op = m.group(3)
            rest = m.group(4)
            rhs = rest.lstrip()
            # Replace balanced <...> grouping with (...) but leave
            # unbalanced > (macro body closers) untouched.
            prev = None
            while prev != rhs:
                prev = rhs
                rhs = re.sub(r'<([^<>]*)>', r'(\1)', rhs)
            directive = 'SET' if symbol == 'Q' else 'EQU'
            result.append((lineno, f"{symbol} {directive} {rhs}"))
        else:
            result.append((lineno, line))
    return result


# ---------------------------------------------------------------------------
# Pass 4: Conditional Assembly
# ---------------------------------------------------------------------------

def pass4_conditional_assembly(numbered_lines, warnings):
    """Convert IFE, IFN, IF1 to ASL IF/ENDIF style."""
    result = []
    i = 0
    while i < len(numbered_lines):
        lineno, line = numbered_lines[i]

        # Match IF1,<
        m_if1 = re.match(r'^(\s*(?:[.A-Za-z_]\w*:\s*)?)IF1\s*,\s*<(.*)$', line)
        if m_if1:
            label = m_if1.group(1).strip()
            rest = m_if1.group(2)
            if label:
                result.append((lineno, label))
            result.append((lineno, f"{FIXED_INDENT}IF 1"))
            i = _collect_conditional_body(
                numbered_lines, i, lineno, rest, result, warnings)
            continue

        # Match IFE/IFN expr,<...> (greedy: last ,< wins, handles <> in exprs)
        m_ifx = re.match(
            r'^(\s*(?:[.A-Za-z_]\w*:\s*)?)(IFE|IFN|IFNDEF)\s+(.*),\s*<(.*)$',
            line
        )
        if m_ifx:
            label = m_ifx.group(1).strip()
            kind = m_ifx.group(2)
            expr = _replace_angle_brackets(m_ifx.group(3).rstrip())
            rest = m_ifx.group(4)
            if label:
                result.append((lineno, label))
            if kind == 'IFE':
                result.append((lineno, f"{FIXED_INDENT}IF {expr}=0"))
            elif kind == 'IFN':
                result.append((lineno, f"{FIXED_INDENT}IF {expr}<>0"))
            elif kind == 'IFNDEF':
                result.append((lineno, f"{FIXED_INDENT}IF ~~DEFINED({expr})<>0"))
            else:
                pass
            i = _collect_conditional_body(
                numbered_lines, i, lineno, rest, result, warnings)
            continue

        # Missing comma: IFE expr<body> (OCR dropped the comma)
        m_ifx_nocomma = re.match(
            r'^(\s*(?:[.A-Za-z_]\w*:\s*)?)(IFE|IFN)\s+(\S+)<(.*)$',
            line
        )
        if m_ifx_nocomma:
            label = m_ifx_nocomma.group(1).strip()
            kind = m_ifx_nocomma.group(2)
            expr = _replace_angle_brackets(m_ifx_nocomma.group(3).rstrip())
            rest = m_ifx_nocomma.group(4)
            if label:
                result.append((lineno, label))
            warnings.warn(lineno, "OCR",
                f"Missing comma before '<' in conditional: {line.strip()}",
                f"Change to: {kind}     {expr},<{rest}")
            if kind == 'IFE':
                result.append((lineno, f"{FIXED_INDENT}IF {expr}=0"))
            else:
                result.append((lineno, f"{FIXED_INDENT}IF {expr}<>0"))
            i = _collect_conditional_body(
                numbered_lines, i, lineno, rest, result, warnings)
            continue

        result.append((lineno, line))
        i += 1

    return result


def _indent_cond_body(text):
    """Indent a conditional body line unless it is a label, assignment, or already indented."""
    stripped = text.lstrip()
    if not stripped:
        return text
    if re.match(r'^[.A-Za-z_]\w*:', stripped):
        return stripped
    if re.match(r'^[.A-Za-z_]\w*==?', stripped):
        return stripped
    if re.match(r'^[.A-Za-z_]\w*\s+EQU\b', stripped):
        return stripped
    if re.match(r'^COMMENT\s', stripped):
        return stripped
    if text[0:1] in (' ', '\t'):
        return text
    return FIXED_INDENT + stripped


def _collect_conditional_body(numbered_lines, i, start_lineno,
                              first_rest, result, warnings):
    """Collect lines inside a conditional block delimited by < >.

    Body lines are recursively processed through pass4.
    Returns the next line index to process.
    """
    depth = 1
    current = first_rest
    current_lineno = start_lineno
    body_lines = []

    while True:
        line_content = ""
        j = 0
        while j < len(current):
            ch = current[j]
            if ch == '<':
                depth += 1
                line_content += ch
            elif ch == '>':
                depth -= 1
                if depth == 0:
                    if line_content.strip():
                        indented = _indent_cond_body(line_content)
                        body_lines.append((current_lineno, indented))
                    converted = pass4_conditional_assembly(body_lines, warnings)
                    result.extend(converted)
                    result.append((current_lineno, f"{FIXED_INDENT}ENDIF"))
                    return i + 1
                else:
                    line_content += ch
            else:
                line_content += ch
            j += 1

        body_lines.append((current_lineno, _indent_cond_body(current)))

        i += 1
        if i >= len(numbered_lines):
            warnings.warn(start_lineno, "OCR",
                f"Conditional block starting here is never closed "
                f"(no matching '>' found before end of file).",
                f"Add '>' to close the conditional block that opens "
                f"on line {start_lineno}.")
            converted = pass4_conditional_assembly(body_lines, warnings)
            result.extend(converted)
            result.append((current_lineno, f"{FIXED_INDENT}ENDIF"))
            return i
        current_lineno, current = numbered_lines[i]


# ---------------------------------------------------------------------------
# Pass 5: String Constants
# ---------------------------------------------------------------------------

def pass5_string_constants(numbered_lines, warnings):
    """Convert DC"string" to DB 'string' and handle ACRLF."""
    result = []
    i = 0
    while i < len(numbered_lines):
        lineno, line = numbered_lines[i]

        # Check for ACRLF (possibly with a label prefix)
        m_acrlf = re.match(r'^(\s*\w*:?\s*)ACRLF\s*$', line)
        if m_acrlf:
            prefix = m_acrlf.group(1)
            # Look ahead for DC"string" or bare 0
            next_i = i + 1
            while next_i < len(numbered_lines) and \
                  numbered_lines[next_i][1].strip() == '':
                next_i += 1

            if next_i < len(numbered_lines):
                next_lineno, next_line = numbered_lines[next_i]
                m_dc = re.match(
                    r'^(\s*\w*:?\s*)DC"([^"]*)"(.*)$', next_line)
                if m_dc:
                    string_content = m_dc.group(2)
                    dc_rest = m_dc.group(3).strip()
                    comment = f"  {dc_rest}" if dc_rest else ""

                    # Check for trailing bare 0
                    after_dc = next_i + 1
                    while after_dc < len(numbered_lines) and \
                          numbered_lines[after_dc][1].strip() == '':
                        after_dc += 1

                    if after_dc < len(numbered_lines) and re.match(
                        r'^\s*0\s*$', numbered_lines[after_dc][1]
                    ):
                        result.append((lineno,
                            f"{prefix}DB 13, 10, '{string_content}', 0{comment}"))
                        i = after_dc + 1
                    else:
                        result.append((lineno,
                            f"{prefix}DB 13, 10, '{string_content}'{comment}"))
                        i = next_i + 1
                    continue

                if re.match(r'^\s*0\s*$', next_line):
                    result.append((lineno, f"{prefix}DB 13, 10, 0"))
                    i = next_i + 1
                    continue

                result.append((lineno, f"{prefix}DB 13, 10"))
                i += 1
                continue
            else:
                result.append((lineno, f"{prefix}DB 13, 10"))
                i += 1
                continue

        # Check for DC"string" (without preceding ACRLF)
        m_dc = re.match(r'^(\s*\w*:?\s*)DC"([^"]*)"(.*)$', line)
        if m_dc:
            prefix = m_dc.group(1)
            string_content = m_dc.group(2)
            dc_rest = m_dc.group(3).strip()
            comment = f"  {dc_rest}" if dc_rest else ""

            next_i = i + 1
            while next_i < len(numbered_lines) and \
                  numbered_lines[next_i][1].strip() == '':
                next_i += 1

            if next_i < len(numbered_lines) and re.match(
                r'^\s*0\s*$', numbered_lines[next_i][1]
            ):
                result.append((lineno,
                    f"{prefix}DB '{string_content}', 0{comment}"))
                i = next_i + 1
            else:
                result.append((lineno,
                    f"{prefix}DB '{string_content}'{comment}"))
                i += 1
            continue

        result.append((lineno, line))
        i += 1

    return result


# ---------------------------------------------------------------------------
# Pass 6: COMMENT Blocks
# ---------------------------------------------------------------------------

def pass6_comment_blocks(numbered_lines, warnings):
    """Convert MACRO-10 COMMENT blocks to ;-prefixed comment lines.

    A COMMENT block starts with 'COMMENT' in column 1 followed by a
    delimiter character.  It continues until a line whose sole content
    is that delimiter character.  The COMMENT line and the trailing
    delimiter line are removed; every body line is prefixed with ';'.
    """
    result = []
    i = 0
    while i < len(numbered_lines):
        lineno, line = numbered_lines[i]

        m = re.match(r'^\s*COMMENT\s(.)(.*)$', line)
        if not m:
            result.append((lineno, line))
            i += 1
            continue

        delim = m.group(1)
        # If there is text after the delimiter on the COMMENT line, treat
        # it as the first body line.
        first_rest = m.group(2)
        if first_rest:
            result.append((lineno, ';' + first_rest))

        i += 1
        while i < len(numbered_lines):
            blineno, bline = numbered_lines[i]
            if bline.strip() == delim:
                # Trailing delimiter line — skip it
                i += 1
                break
            result.append((blineno, ';' + bline))
            i += 1
        else:
            warnings.warn(lineno, "SYNTAX",
                f"COMMENT block starting here is never closed "
                f"(no matching '{delim}' found before end of file).",
                f"Add a line containing only '{delim}' to close the "
                f"COMMENT block that opens on line {lineno}.")

    return result


# ---------------------------------------------------------------------------
# Pass 7: PDP-10 Directives
# ---------------------------------------------------------------------------

_PDP10_DIRECTIVES = re.compile(
    r'^\s*(PAGE|SUBTTL|TITLE|RADIX|SEARCH|INTERNAL|EXTERNAL|MCSSIM|PRINTX|SALL|LIST|XLIST)\b'
)


def pass7_pdp10_directives(numbered_lines, warnings):
    """Comment out PDP-10-AS page control and assembler directives."""
    result = []
    for lineno, line in numbered_lines:
        if _PDP10_DIRECTIVES.match(line):
            result.append((lineno, ';' + line))
        else:
            result.append((lineno, line))
    return result


# ---------------------------------------------------------------------------
# Pass 8: $CODE Symbol
# ---------------------------------------------------------------------------

def pass8_code_symbol(numbered_lines, warnings):
    """Replace $CODE with CODE and insert a CODE EQU 0 definition at the top."""
    result = [(0, 'CODE    EQU     0')]
    result.append((1, 'XWD    MACRO     param1,param2'))
    result.append((2, '        DB      param2'))
    result.append((3, '       ENDM'))
    result.append((4, 'DC     MACRO     param1'))
    result.append((5, '        DB      param1'))
    result.append((6, '       ENDM'))
    for lineno, line in numbered_lines:
        result.append((lineno, line.replace('$CODE', 'CODE')))
    return result


# ---------------------------------------------------------------------------
# Pass 9: Dollar-sign Symbols
# ---------------------------------------------------------------------------

_DOLLAR_SUBS = [
    ('RIGHT$', 'RIGHT_S'),
    ('LEFT$',  'LEFT_S'),
    ('MID$',   'MID_S'),
    ('CHR$',   'CHR_S'),
    ('STR$',   'STR_S'),
    ('##',     ''),
]


def pass9_dollar_symbols(numbered_lines, warnings):
    """Replace dollar-sign symbol names with underscore-S equivalents."""
    result = []
    for lineno, line in numbered_lines:
        for old, new in _DOLLAR_SUBS:
            line = line.replace(old, new)
        result.append((lineno, line))
    return result


# ---------------------------------------------------------------------------
# Pass 10: Directive Rewrites
# ---------------------------------------------------------------------------

def pass10_directive_rewrites(numbered_lines, warnings):
    """Rewrite miscellaneous PDP-10 directives to ASL equivalents.

    - Bare number on a line -> DB number
    - BLOCK n -> DS n
    - RELOC n -> ORG n
    - ADR(n) -> DW n
    """
    result = []
    for lineno, line in numbered_lines:
        # Bare single-char string: "X"<rest> -> DB 'X'<rest>
        m_sqc = re.match(r'^\s+"(.)"(.*)', line)
        if m_sqc:
            ch = m_sqc.group(1)
            rest = m_sqc.group(2)
            if rest:
                result.append((lineno, f"{FIXED_INDENT}DB      '{ch}'{rest}"))
            else:
                result.append((lineno, f"{FIXED_INDENT}DB      '{ch}'"))
            continue

        # Bare number (with optional label: prefix)
        m_num = re.match(r'^(\s*[.A-Za-z_]\w*:\s+|\s*)(\d\S*)\s*(;.*)?$', line)
        if m_num:
            label = m_num.group(1) or ''
            num = m_num.group(2)
            comment = m_num.group(3) or ''
            if comment:
                result.append((lineno, f"{label}{FIXED_INDENT}DB      {num}  {comment}"))
            else:
                result.append((lineno, f"{label}{FIXED_INDENT}DB      {num}"))
            continue

        # BLOCK -> DS (with optional label)
        m_block = re.match(r'^(\s*(?:[.A-Za-z_]\w*:?\s+)?)BLOCK\s+(.*)', line)
        if m_block:
            prefix = m_block.group(1)
            rest = m_block.group(2)
            result.append((lineno, f"{prefix}DS      {rest}"))
            continue

        # RELOC -> ORG
        m_reloc = re.match(r'^\s*RELOC\s+(.*)', line)
        if m_reloc:
            rest = m_reloc.group(1)
            result.append((lineno, f"{FIXED_INDENT}ORG     {rest}"))
            continue

        # EXP -> DB
        m_exp = re.match(r'^\s*EXP\s+(.*)', line)
        if m_exp:
            rest = m_exp.group(1)
            result.append((lineno, f"{FIXED_INDENT}DB     {rest}"))
            continue

        # ADRP(x) -> ADRP x (remove parens only)
        m_adrp = re.match(r'^(\s*(?:[.A-Za-z_]\w*:?\s*)?)ADRP\(([^)]+)\)(.*)', line)
        if m_adrp:
            prefix = m_adrp.group(1)
            if not prefix.strip():
                prefix = FIXED_INDENT
            arg = m_adrp.group(2)
            rest = m_adrp.group(3).strip()
            if rest:
                result.append((lineno, f"{prefix}ADRP    {arg}  {rest}"))
            else:
                result.append((lineno, f"{prefix}ADRP    {arg}"))
            continue

        # ADR(x) -> DW x
        m_addr = re.match(r'^(\s*(?:[.A-Za-z_]\w*:?\s*)?)ADR\(([^)]+)\)(.*)', line)
        if m_addr:
            prefix = m_addr.group(1)
            if not prefix.strip():
                prefix = FIXED_INDENT
            arg = m_addr.group(2)
            rest = m_addr.group(3).strip()
            if rest:
                result.append((lineno, f"{prefix}DW      {arg}  {rest}"))
            else:
                result.append((lineno, f"{prefix}DW      {arg}"))
            continue

        # Opcode/macro directly abutting a quote -> insert space
        m_quote = re.match(r'^(\s*(?:[.A-Za-z_]\w*:\s*)?)([A-Za-z_]\w*)"(.*)', line)
        if m_quote:
            prefix = m_quote.group(1)
            opcode = m_quote.group(2)
            rest = m_quote.group(3)
            line = f'{prefix}{opcode} "{rest}'

        # name MACRO rest -> name<indent>MACRO<indent>rest (strip leading whitespace)
        m_macro = re.match(r'^\s*(\w+)\s+MACRO\b(.*)', line)
        if m_macro:
            name = m_macro.group(1)
            rest = m_macro.group(2).strip()
            if rest:
                result.append((lineno, f"{name}{FIXED_INDENT}MACRO{FIXED_INDENT}{rest}"))
            else:
                result.append((lineno, f"{name}{FIXED_INDENT}MACRO"))
            continue

        result.append((lineno, line))
    return result


# ---------------------------------------------------------------------------
# Pass 99: Final Lint / Formatting
# ---------------------------------------------------------------------------

def final_lint_formatting(numbered_lines, warnings):
    """Normalize whitespace to use hard tabs and consistent column layout.

    - Comment lines (starting with ;) are left completely alone.
    - Blank lines are left alone.
    - EQU lines:   LABEL\\tEQU\\tVALUE\\t\\tCOMMENT  (left-justified)
    - IF/IF1:      IF\\texpression  (left-justified)
    - ENDIF:       ENDIF  (left-justified)
    - MACRO/ENDM:  NAME\\tMACRO\\tPARAMS  (left-justified)
    - Label-only:  left-justified
    - Instructions: \\tOPCODE\\tOPERANDS\\t\\tCOMMENT
    """
    result = []
    for lineno, line in numbered_lines:
        # Blank lines
        if not line.strip():
            result.append((lineno, ''))
            continue

        # Comment lines — leave completely alone
        stripped = line.lstrip()
        if stripped.startswith(';'):
            result.append((lineno, line))
            continue

        # Split off trailing comment (;) from the code portion
        code_part, comment = _split_comment(line)
        code_part = code_part.rstrip()

        # Tokenize code_part: optional label:, then remaining tokens
        label = ''
        rest_code = code_part.lstrip()
        m_label = re.match(r'^([.A-Za-z_]\w*:)\s*(.*)', rest_code)
        if m_label:
            label = m_label.group(1)
            rest_code = m_label.group(2).strip()

        # Split rest into words for opcode detection
        tokens = rest_code.split(None, 1)
        first_word = tokens[0] if tokens else ''
        after_first = tokens[1].strip() if len(tokens) > 1 else ''

        # Detect SYMBOL EQU/SET value: first_word is symbol name, after is "EQU ..." or "SET ..."
        # This handles lines like "CODE EQU 0" or "Q SET 128-1"
        equ_match = re.match(r'^(EQU|SET)\b\s*(.*)', after_first)
        if not label and first_word and equ_match:
            sym = first_word
            directive = equ_match.group(1)
            value = equ_match.group(2).strip()
            if comment:
                result.append((lineno, f"{sym}\t{directive}\t{value}\t\t{comment}"))
            else:
                result.append((lineno, f"{sym}\t{directive}\t{value}"))
            continue

        # Detect NAME MACRO params (no colon label)
        macro_match = re.match(r'^MACRO\b\s*(.*)', after_first)
        if not label and first_word and macro_match:
            name = first_word
            params = macro_match.group(1).strip()
            if comment:
                result.append((lineno, f"{name}\tMACRO\t{params}\t\t{comment}"))
            elif params:
                result.append((lineno, f"{name}\tMACRO\t{params}"))
            else:
                result.append((lineno, f"{name}\tMACRO"))
            continue

        # From here, use first_word as the opcode
        opcode = first_word
        operands = after_first

        # EQU with label: (LABEL: EQU value)
        if opcode == 'EQU':
            sym = label.rstrip(':') if label else ''
            if comment:
                result.append((lineno, f"{sym}\tEQU\t{operands}\t\t{comment}"))
            else:
                result.append((lineno, f"{sym}\tEQU\t{operands}"))
            continue

        # Label-only line
        if label and not opcode and not comment:
            result.append((lineno, label))
            continue
        if label and not opcode and comment:
            result.append((lineno, f"{label}\t\t\t{comment}"))
            continue

        # IF / IF1 lines: tab-indented
        if opcode in ('IF', 'IF1'):
            if comment:
                result.append((lineno, f"\t{opcode}\t{operands}\t\t{comment}"))
            else:
                result.append((lineno, f"\t{opcode}\t{operands}"))
            continue

        # ENDIF: tab-indented
        if opcode == 'ENDIF':
            if comment:
                result.append((lineno, f"\tENDIF\t\t\t{comment}"))
            else:
                result.append((lineno, '\tENDIF'))
            continue

        # ENDM: tab-indented
        if opcode == 'ENDM':
            result.append((lineno, '\tENDM'))
            continue

        # Normal instruction/directive line
        # label: on its own line, then \tOPCODE\tOPERANDS\t\tCOMMENT
        prefix = ''
        if label:
            prefix = label + '\n'

        if operands and comment:
            formatted = f"{prefix}\t{opcode}\t{operands}\t\t{comment}"
        elif operands:
            formatted = f"{prefix}\t{opcode}\t{operands}"
        elif comment:
            formatted = f"{prefix}\t{opcode}\t\t\t{comment}"
        else:
            formatted = f"{prefix}\t{opcode}"

        result.append((lineno, formatted))

    return result


def _split_comment(line):
    """Split a line into code and comment parts, respecting quoted strings."""
    in_single = False
    in_double = False
    for i, ch in enumerate(line):
        if ch == "'" and not in_double:
            in_single = not in_single
        elif ch == '"' and not in_single:
            in_double = not in_double
        elif ch == ';' and not in_single and not in_double:
            return line[:i], line[i:]
    return line, ''


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def main():
    if len(sys.argv) != 3:
        print(f"Usage: {sys.argv[0]} <input_file> <output_file>",
              file=sys.stderr)
        sys.exit(1)

    input_file = sys.argv[1]
    output_file = sys.argv[2]

    with open(input_file, 'r') as f:
        raw_lines = [line.rstrip('\n') for line in f]

    # Build (lineno, text) tuples — lineno is 1-based
    numbered_lines = [(i + 1, text) for i, text in enumerate(raw_lines)]

    warnings = Warnings(input_file)

    # Apply passes sequentially
    numbered_lines = pass0_strip_end_block(numbered_lines, warnings)
    numbered_lines = pass1_numeric_constants(numbered_lines, warnings)
    numbered_lines = pass2_macro_definitions(numbered_lines, warnings)
    numbered_lines = pass3_equ_conversion(numbered_lines, warnings)
    numbered_lines = pass4_conditional_assembly(numbered_lines, warnings)
    numbered_lines = pass3_equ_conversion(numbered_lines, warnings)  # re-run for bodies extracted by pass4
    numbered_lines = pass5_string_constants(numbered_lines, warnings)
    numbered_lines = pass6_comment_blocks(numbered_lines, warnings)
    numbered_lines = pass7_pdp10_directives(numbered_lines, warnings)
    numbered_lines = pass8_code_symbol(numbered_lines, warnings)
    numbered_lines = pass9_dollar_symbols(numbered_lines, warnings)
    numbered_lines = pass10_directive_rewrites(numbered_lines, warnings)
    numbered_lines = final_lint_formatting(numbered_lines, warnings)

    # Write output
    with open(output_file, 'w') as f:
        for lineno, line in numbered_lines:
            f.write(line + '\n')

    # Print warnings
    if warnings.count > 0:
        print(f"\n{'='*60}", file=sys.stderr)
        print(f"Conversion complete: {input_file} -> {output_file}",
              file=sys.stderr)
        print(f"{warnings.count} warning(s) found — these likely indicate "
              f"OCR errors in the input file.", file=sys.stderr)
        print(f"{'='*60}\n", file=sys.stderr)
        warnings.print_all()
        print(file=sys.stderr)
    else:
        print(f"Converted {input_file} -> {output_file} (no warnings)",
              file=sys.stderr)


if __name__ == '__main__':
    main()
