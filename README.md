# ALTAIR BASIC 3.0

This is a working Altair BASIC from the original source. After much OCR work,
it was converted to 8080 MACRO assembler. A variety of approaches were taken
to implement missing portions of the original source.

The assembled source runs in an emulator, and games from the 1970's can be pasted
into it and run.

The source for the 3.0 release of ALTAIR BASIC was transcribed from the printouts
[released by Bill Gates](https://www.gatesnotes.com/microsoft-original-source-code)
in April 2025 for the 50th anniversary of Microsoft.

This source is used by an accompanying project to explore and educate others about its inner workings:
[Decoded: ALTAIR BASIC](https://maizure.org/projects/decoded-altair-basic/index.html).

This repository is forked from Mr. MaiZure's git repository. His goal was to reach
a 100% accurate transcription of the line printer source to text files.

My primary goal has been to produce a baseline working Altair BASIC from original source that
I and others can then modify in various ways later.

## Building

Download ASL from http://john.ccac.rwth-aachen.de:8000/as/ - compile it and
install the output binaries into your ~/bin directory.

Once you've done that, just run the assembler script:

./asm.sh ./asm.sh ALTAIRBASIC30.asm 

The outputs are:
 * ALTAIRBASIC30.bin a raw binary intended to be loaded at 0000H
 * ALTAIRBASIC30.hex intel hex format version of above
 * ALTAIRBASIC30.lst the output listing
 * ALTAIRBASIC30.sym the symbols that can be used by i8080tool during disassembly and tracing

## Running

I did all my testing using the program i8080tool that I wrote.

Building it requires the usual Linux development environment tools - gcc,
g++ (requires C++ 20), git, make, and cmake.

Just run the following commands to build it:

cd i8080tool && cmake .
make

I usually copy the resulting i8080tool program to my ~/bin diectory.

Then, you can run basic by doing this:

i8080tool ALTAIRBASIC30.bin

I just copy/paste programs to it and run them.

I love playing [super star trek](https://github.com/GReaperEx/bcg/blob/master/superstartrek.bas)

Note: when I dock at a starbase, I get a RETURN WITHOUT GOSUB error - I do
not know if that is a star trek program error or an ALTAIRBASIC30.bin
error. Time permitting, I will figure that out.

## Files in this repo

| File      | Purpose      |
| ------------- | ------------- |
| ALTAIRBASIC30.LST     | The master transcribed file from which all others are generated     |
| ALTAIRBASIC30_ASM_LINES.LST   | All source with only assembly including line numbers   |
| ALTAIRBASIC30_ASM_NOLINES.LST   | All source with only assembly without line numbers  |
| ALTAIRBASIC30.asm   | Working ssembly code for the 8080 BASIC interpreter version 3.0 |
| 'Extended BASIC Ver 4-1.tap'  | Believed good binary reference of a later version (4.1) of the same code|
| 'Extended BASIC Ver 4-1.sym'  | Incomplete manual and machine generated symbols of the 4.1 reference |
| asm.sh  | Helper script to run the assembler |
| convert.py | Machine and human generated program to convert PDP-10 AS to 8080 MACRO assembler |

Note - for now, download [Extended BASIC version 4.1](https://deramp.com/downloads/altair/software/papertape_cassette/BASIC%20with%2088-2SIO%20as%20Cassette/)

## Project goals

We have an mostly complete Intel 8080 assembler file ALTAIRBASIC30.asm. It implements
a dialect of the BASIC programming language that will be referred to as Altair BASIC.

We have a reference binary that is a later version of this exact assembler program.
This reference binary is known to be complete and working.

This reference binary is available only in binary form - we do not have assembler
code for it.

Disassemblers can work, but the experiment we are undertaking is to do
runtime disassembly, and see if we can obtain missing subroutines in that
manner.

My ultimate goal is to finish ALTAIRBASIC30.asm so that it not only
assembles to a binary, but also runs in a similar fashion as reference
binaries that are known to be good and can run BASIC programs.

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

At this point, Altair Basic 3.0 has all of the functionality it needs to run
games.

At the time Micro-Soft Altair BASIC 3.0 was written, some features were not
yet complete.

These include LLIST/LPRINT commands for line printer output, PRINT USING
for formatted output, and possibly incomplete disk I/O routines.

### LLIST/LPRINT

These commands are present, and can be enabled by defining 'LPTSW' to 1
in the assembler source.

My attempts to make it work failed, mostly because it ended up not being
that useful - they do control a line printer, but it has a control
register that is used to, for example, emit a newline.

It could be made to work, but will require more cleanup than I care to do at
the moment.

### PRINT USING

The keyword USING (token name USINTK) is listed in the table of tokens, but is not used anywhere.

I would like this feature to work, but will require some intensive work with
the floating point code, or more reverse engineering of the 4.1 reference.

Either way, it will be a moderately large amount of work.

### Missing 'EDIT' command subroutines

None of this EDIT command assembly code is available, so as a result, we need to find the entry
point in the reference, disassemble it as best we can and see if we can implement that
in Altair BASIC 3.0.

It is an interfactive keyboard based line edit capability, and provides very
simple edit function.

It is not a particularly flexible or useful editor, so the value in
finishing it is not high.
