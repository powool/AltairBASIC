#!/bin/bash
#
# http://john.ccac.rwth-aachen.de:8000/as/
#
# untar, then:
# cd asl-current
# cp Makefile.def-samples/Makefile.def-unknown-linux Makefile.def
# make
# copy binaries somewhere in your $PATH

dir=$(dirname "$1")
base=$(basename "$1" .asm)
base=$(basename $base .ASM)

asl -l -cpu 8080 "$1" > $dir/$base.tmp
if grep '^PASS 2' < $dir/$base.tmp ; then
	awk 'BEGIN {p = 0;} { if(p) print; } /^PASS 2/ { p = 1;}' $dir/$base.tmp > $dir/$base.lst
	rm $dir/$base.tmp
else
	mv $dir/$base.tmp $dir/$base.lst
fi
if [ $? -ne 0 ] ; then
	echo "Assembly failed. Script terminating."
	exit 1
fi

# asl will correctly string either .ASM or .asm,
# so do that here:

p2hex -F Intel $dir/$base.p

