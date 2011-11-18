import datetime
import math
import sys

import cloudaws

SDBDomain = 'TestRuns'

aws = cloudaws.AWS('aws.keys')

now = datetime.datetime.utcnow()

user = raw_input('Your e-mail address: ')
testId = '%s-%s' % (user, now.strftime('%Y-%m-%d-%H:%M:%S'))
testList = list()
counter = 0

def addOneTest(name, url, duration, concurrency, thinkTime, keepAlive):
  testList.append({'url' : url, 'duration' : duration,
                   'concurrency' : concurrency, 'keepAlive' : keepAlive,
                   'thinkTime' : thinkTime,
                   'owner' : user, 'run' : testId, 'name' : name })

def addTest(name, url, duration, concurrency, thinkTime, keepAlive):
  # Add test instances in powers of 10 up to the selected level
  topLog = int(math.log10(concurrency))
  for l in range(topLog + 1):
     addOneTest(name, url, duration, int(math.pow(10, l)), thinkTime, keepAlive)
  if concurrency != int(math.pow(10, topLog)):
     addOneTest(name, url, duration, concurrency, thinkTime, keepAlive)

aws.createDomain(SDBDomain)

another = 'y'
while another == 'y' or another == 'Y':
  name = raw_input('Name for this test run: ')
  url = raw_input('URL to test: ')
  duration = raw_input('Test Duration (in minutes): ')
  concurrency = raw_input('Maximum concurrency: ')
  thinkTime = raw_input('Think time (in milliseconds): ')
  ka = raw_input('HTTP Keep-Alive? (y/n) ')
  keepAlive = False
  if ka == 'y' or ka == 'Y':
    keepAlive = True
  addTest(name, url, int(duration), int(concurrency), int(thinkTime), keepAlive)
  another = raw_input('Add another test? (y/n) ')

for i in range(len(testList)):
  li = testList[i]
  name = '%s-%i' % (testId, i)
  li['count'] = i
  aws.put(SDBDomain, name, li)

print 'Wrote out database records for ', testId
