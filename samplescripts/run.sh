#!/bin/sh

N=run9-4

DIRECT=http://10.20.33.203:9010/bench
SN=http://10.20.9.204/bench
SNS=https://10.20.9.204/bench

TEE="tee -a ${N}.csv"
#TEE=

. ./env.sh
${APIB} -T |tee ${N}.csv

MONITOR=10.20.13.203:9011
export MONITOR
MONITOR2=10.20.13.203:9011
export MONITOR2

#./rungroup.sh 00-direct ${DIRECT}/passthrough |${TEE}
#./rungroup.sh 00-direct-nk ${DIRECT}/passthrough -k |${TEE}

MONITOR=10.20.9.204:9010
export MONITOR

./rungroup.sh 01 ${SN}/bench |${TEE}
./rungroup.sh 01-nk ${SN}/bench -k |${TEE}

./rungroup.sh 02 ${SNS}/passthrough |${TEE}
./rungroup.sh 02-nk ${SNS}/passthrough -k |${TEE}

./rungroup.sh 03 ${SNS}/extractKey?apikey=xxxx |${TEE}
./rungroup.sh 03-nk ${SNS}/extractKey?apikey=xxxx -k |${TEE}

EXTRA="-t application/xml -f input.xml"
export EXTRA

./rungroup.sh 04 ${SNS}/transform |${TEE}
./rungroup.sh 04-nk ${SNS}/transform -k |${TEE}
