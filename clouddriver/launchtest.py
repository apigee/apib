import csv
import datetime
import math
import subprocess
import StringIO
import sys

import cloudaws

apibProgram = '/usr/local/bin/apib'
DefaultWarmup = '60'

AWSKeys='aws.keys'
TestDefs='TestDefs'
TestResults='TestResults'

def getCount(r):
    return r['count']

def runTest(count, url, duration, concurrency, thinkTime):
# TODO thinkTime
    args = [apibProgram, '-S', 
            '-w', DefaultWarmup,
            '-d', str(duration),
            '-W', str(thinkTime),
            '-c', str(concurrency),
            url]

    result = subprocess.check_output(args)
    resultFile = StringIO.StringIO(result)
    csvRdr = csv.reader(resultFile)
    for row in csvRdr:
        result = {
            'runName' : RunName,
            'defName' : DefName,
            'throughput' : row[1],
            'avg_latency' : row[2],
            'completed' : row[6],
            'successful' : row[7],
            'errors' : row[8],
            'sockets_opened' : row[9],
            'latency_min' : row[10],
            'latency_max' : row[11],
            'latency_50' : row[12],
            'latency_90' : row[13],
            'latency_98' : row[14],
            'latency_99' : row[15],
            'latency_std_dev' : row[16],
            'client_cpu' : row[17],
            'run_time' : str(datetime.datetime.now()) 
            'url' : url,
            'duration' : str(duration),
            'concurrency' : str(concurrency),
            'thinkTime' : str(thinkTime)
            }

        aws.put(TestResults, '%s-%i' % (RunName, count), result)

if len(sys.argv) != 2:
    print 'Usage: launchtest <metadata>'
    sys.exit(1)

mData = StringIO.StringIO(sys.argv[1])
mRdr = csv.reader(mData)
for row in mRdr:
    RunName = row[0]
    DefName = row[1]

aws = cloudaws.AWS(AWSKeys)

testDef = aws.get(TestDefs, DefName)
count = 0

url = testDef['url']
duration = int(testDef['duration']) * 60
maxConcurrency = int(testDef['max_concurrency'])
thinkTime= int(testDef['think_time'])
count = 0

topLog = int(math.log10(maxConcurrency))
for l in range(topLog + 1):
    runTest(count, url, duration, int(math.pow(10, l)), thinkTime)
    count = count + 1
if maxConcurrency != int(math.pow(10, topLog)):
    runTest(count, url, duration, maxConcurrency, thinkTime)


    
    
