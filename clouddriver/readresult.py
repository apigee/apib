import sys

import cloudaws

def getCount(r):
    return r['count']

AWSDomain = 'TestRuns'

if len(sys.argv) != 2:
    print 'Usage: launchtest <test id>'
    sys.exit(1)

testId = sys.argv[1]
aws = cloudaws.AWS('aws.keys')

runs = aws.select("select * from TestRuns where run = '%s'" % testId)

runVals = runs.values()
for r in runVals:
    r['count'] = int(r['count'])
runVals.sort(key=getCount)

for r in runVals:
    for i in r.items():
        print '%s = %s' % i


