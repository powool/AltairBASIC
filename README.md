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
| ALTAIRBASIC30_asm   | Incomplete assembly code for the 8080 BASIC interpreter  |


---

## Project goals

We have an incomplete Intel 8080 assembler file ALTAIRBASIC30.asm. The existing
assembly is an incomplete implementation of a BASIC interpreter.

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

## Tools

Initially, we have a simulator program that correctly runs the reference
binary. It can be used to examine instruction sequences as it progresses
through program start, initialization, and simple statements.

The simulator is in the directory i8080\_core, the main program is i8080\_core/i8080\_test.cpp.

Notably, the simulator can dump program state, with Intel 8080 source disassembly.
It can also limit execution to a small number of instructions.
Lastly, the keyboard input, implemented in i8080\_hal.c will first emulate
the user typing the keystrokes to handle the initial setup of the reference.

Because the available ALTAIRBASIC30.asm is older, it will not have the same
exact startup after asking about available memory.

Depending on how things progress, we may need to implement new tools to help us.

