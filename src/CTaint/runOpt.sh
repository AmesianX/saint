#!/bin/bash

USAGE="Usage: $(basename $0) <-i bc files>"

if [ $# -lt 1 ]; then
  echo "$USAGE"
  exit 1
fi

moduleflag=
fileflag=

while getopts 'mfi:' OPTION
do
  case $OPTION in
    m)	moduleflag=1
	;;
    i)	fileflag=1
      	INPUTFILE="$OPTARG"
	echo "Input file: $INPUTFILE"
	;;
    ?)	echo "$USAGE" >&2
        exit 2
	;;
  esac
done
shift $(($OPTIND - 1))

function run_cmd(){
  local CMD="$1"
  echo "$CMD"
  eval "$CMD"
}

if [ ! "$fileflag" ]; then
  echo "A .bc file must be given to run the analysis"
  echo "$USAGE"
  exit 2
fi

PASSARG="-ctaintmod"

OPT=/home/noundou/tools/llvm-3.3.src/build/Release+Asserts/bin/opt

COMPILE="make -f Makefile.ctaint compile > /dev/null"
run_cmd "$COMPILE"

CMD="$OPT -load $LLVM_LIB/LLVMDataStructure.so \
   -load $LLVM_LIB/CTaint.so -calltarget-eqtd "$PASSARG" < "$INPUTFILE" > /dev/null"

run_cmd "$CMD"

