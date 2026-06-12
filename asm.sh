#!/bin/bash
#
# download ASL from:
#
# http://john.ccac.rwth-aachen.de:8000/as/
#
# untar, then:
# cd asl-current
# cp Makefile.def-samples/Makefile.def-unknown-linux Makefile.def
# make
# copy binaries somewhere in your $PATH

base=$(basename "$1" .asm)
# base=$(basename $base .ASM)

asl -l -cpu 8080 "$1" > $base.tmp
if grep '^PASS 2' < $base.tmp ; then
	awk 'BEGIN {p = 0;} { if(p) print; } /^PASS 2/ { p = 1;}' $base.tmp > $base.lst
	rm $base.tmp
else
	mv $base.tmp $base.lst
fi
if [ $? -ne 0 ] ; then
	echo "Assembly failed. Script terminating."
	exit 1
fi

# asl will correctly handle either .ASM or .asm,
# so do that here:

p2hex -F Intel $base.p

hex2bin $base.hex

# Extract symbol table from listing to .sym file
awk '
/^  Symbol Table/ { intable=1; next }
/^  ---/ && intable { next }
/^[[:space:]]*$/ && intable { next }
# Stop at end of symbol table (line not matching symbol format)
intable && !/[:|]/ { intable=0 }
intable {
    # Split line on | to get up to 2 symbol entries per line
    n = split($0, halves, "|")
    for (i = 1; i <= n; i++) {
        s = halves[i]
        # Match: optional *, name, :, hex value, type (C or -)
        if (match(s, /\*?([A-Za-z_][A-Za-z_0-9]*) *: *([0-9A-Fa-f]+) +(C|-)/, m)) {
            printf "%s\t%s\t%s\n", m[1], m[2], m[3]
        }
    }
}
' $base.lst | sort > $base.sym
