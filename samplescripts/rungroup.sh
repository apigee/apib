#!/bin/sh

# Copyright 2019 Google LLC
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#    https://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

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
