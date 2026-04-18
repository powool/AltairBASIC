#!/bin/bash
#
# http://john.ccac.rwth-aachen.de:8000/as/
#
# untar, then:
# cd asl-current
# cp Makefile.def-samples/Makefile.def-unknown-linux Makefile.def
# make
# copy binaries somewhere in your $PATH

asl -l -cpu 8080 "$1"
if [ $? -ne 0 ] ; then
	echo "Assembly failed. Script terminating."
	exit 1
fi

# asl will correctly string either .ASM or .asm,
# so do that here:
dir=$(dirname "$1")
base=$(basename "$1" .asm)
base=$(basename $base .ASM)

p2hex -F Intel $base.p

