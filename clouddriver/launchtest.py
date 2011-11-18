import csv
import datetime
import subprocess
import StringIO
import sys

import cloudaws

apibProgram = '/usr/local/bin/apib'
#DefaultWarmup = '60'
DefaultWarmup = '0'
AWSDomain = 'TestRuns'

def getCount(r):
    return r['count']

if len(sys.argv) != 2:
    print 'Usage: launchtest <test id>'
    sys.exit(1)

testId = sys.argv[1]
aws = cloudaws.AWS('aws.keys')

rawRuns = aws.select("select * from TestRuns where run = '%s'"  % testId)

testRuns = list()
for rawItem in rawRuns.items():
    raw = rawItem[1]
    if raw['keepAlive'] == 'False':
        ka = 0
    else:
        ka = 999
    run = {'count' : int(raw['count']),
           'duration' : int(raw['duration']) * 60,
           'concurrency' : int(raw['concurrency']),
           'keepAlive' : ka,
           'thinkTime' : int(raw['thinkTime']),
           'url' : raw['url'],
           'owner' : raw['owner'],
           'name' : raw['name'],
           'itemName' : rawItem[0] }
    testRuns.append(run)
testRuns.sort(key=getCount)
            
for run in testRuns:
    args = [apibProgram, '-S', 
            '-N', run['name'], 
            '-w', DefaultWarmup,
            '-d', str(run['duration']),
            '-W', str(run['thinkTime']),
            '-c', str(run['concurrency']),
            '-k', str(run['keepAlive']),
            run['url']]

    result = subprocess.check_output(args)
    resultFile = StringIO.StringIO(result)
    csvRdr = csv.reader(resultFile)
    for row in csvRdr:
        result = {
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
            }
        aws.put(AWSDomain, run['itemName'], result)

    
    
