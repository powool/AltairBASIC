#!/bin/bash
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

# asl will correctly string either .ASM or .asm,
# so do that here:

p2hex -F Intel $base.p

hex2bin $base.hex
