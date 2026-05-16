# ALTAIR BASIC 3.0

The source for the 3.0 release of ALTAIR BASIC transcribed from the printouts [released](https://www.gatesnotes.com/microsoft-original-source-code) by Bill Gates in April 2025 for the 50th anniversary of Microsoft

This source is used by an accompanying project to explore and educate others about its inner workings:
[Decoded: ALTAIR BASIC](https://maizure.org/projects/decoded-altair-basic/index.html)

---


## Files in this repo

| File      | Purpose      |
| ------------- | ------------- |
| ALTAIRBASIC30.LST     | The master transcribed file from which all others are generated     |
| ALTAIRBASIC30_ASM_LINES.LST   | All source with only assembly including line numbers   |
| ALTAIRBASIC30_ASM_NOLINES.LST   | All source with only assembly without line numbers  |
| ALTAIRBASIC30.asm   | Mostly functional assembly code for the 8080 BASIC interpreter version 3.0 |
| ALTAIRBASIC30.sym | symbol table generated from the asm.sh script |
| 'Extended BASIC Ver 4-1.tap'  | Believed good binary reference of a later version (4.1) of the same code|
| 'Extended BASIC Ver 4-1.sym'  | Manual and automatically generated symbols of the 4.1 reference |


---

## Project goals

We have an mostly complete Intel 8080 assembler file ALTAIRBASIC30.asm. It implements
a dialect of the BASIC programming language and will be referred to as Altair BASIC.

Assembly routines that are not implemented are stubbed in with a RET instruction,
and commented with XXX indicating it needs work.

We have a reference binary that is a later version of this exact assembler program.
This reference binary is known to be complete and working.

This reference binary is available only in binary form - we do not have assembler
code for it.

Disassemblers can work, but the experiment we are undertaking is to do
runtime disassembly, and see if we can obtain missing subroutines in that
manner.

The ultimate goal is to finish ALTAIRBASIC30.asm so that it not only
assembles to a binary, but also runs in a similar fashion as reference
binaries that are known to be good and can run BASIC programs.

It is not necessarily a goal to implement exactly what the refernce does.
This may not be practical, and in any case, will tend to detract from
an otherwise unstated goal of trying to keep the 3.0 assembly file as
nearly origianal as we can.

## Tools

Initially, we have a simulator program that correctly runs the reference
binary. It can be used to examine instruction sequences as it progresses
through program start, initialization, and simple statements.

The simulator has been moved to a new file named i8080tool. It is in the
search path. It has options to either emulate the loaded code, or to
disassemble it. In either case, a pre-existing symbol table file can
be loaded. When pre-loading the symbol table, dump or disassembly
will show labels if they can be found.

Whether you start the reference basic or the asm basic, about the same
input is automatically provided to the emulator.

BASIC interpreter output goes to stdout. Instruction/CPU dump goes to stderr.

Notably, the simulator can dump program state, with Intel 8080 source disassembly.
It can also limit execution to a small number of instructions.

Because the available ALTAIRBASIC30.asm is older, it will not have the same
exact startup after asking about available memory.

Depending on how things progress, we may need to implement new tools to help us.

## Missing capabilities and subroutines

At this point, Altair Basic 3.0 has most of the functionality it needs to run
games.

It has support for double precision arithmetic, which is only partly complete and
definitely needs work.

### Missing floating point routines

 XXX NOT IMPLEMENTED
FOUTND:                         ;normalize double
FOTZRC:                         ;convert to decimal digits
FOUTFX:                         ;fixed formatter output

I implemented the previously missing FOTZER by disassembling the code
from the reference/4.1 version and using that. It seems like a good
fit, but it could have problems, for example, with incorrectly handling
SNG vs DBL vs INTEGER types or destroying registers that the existing
asm/3.0 code expected to preserve.

So far, my attempts to disassemble the reference/4.1 version seems to be
showing that the floating point routines in reference/4.1 evolved and
diverged a bit more than expected from the asm/3.0 version.

### Missing 'EDIT' command subroutines

None of this EDIT command assembly code is available, so as a result, we need to find the entry
point in the reference, disassemble it as best we can and see if we can implement that
in Altair BASIC 3.0.

It is an interfactive keyboard based line edit capability, and provides very
simple edit function.
