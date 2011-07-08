#!/bin/sh

. ./env.sh

if [ \( $# -lt 2 \) -o \( $# -gt 3 \) ]
then
  echo "Usage: rungroup.sh <name> <url> [-k]"
  exit 2
fi

name=$1
url=$2

runTest() {
    sleep $RUNDELAY
    $APIB -M $MONITOR -X $MONITOR2 -S -N $name \
           -w $WARMUP -d $DURATION -K $THREADS -c $1 $KEEP $EXTRA $url
}

if [ $# -gt 2 ]
then
  if [ $3 == "-k" ]
  then
    KEEP="-k 0"
  fi
fi

runTest 1
runTest 10
runTest 50
runTest 100
runTest 1000
runTest 5000
