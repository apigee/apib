from google.appengine.api import users

import datetime

import cloudaws

AWSKeys='aws.keys'
TestDefs='TestDefs'
TestRuns='TestRuns'
TestResults='TestResults'

def checkUser(req, checkName=None):
  user = users.get_current_user()
  if user == None:
      req.error(403)
      return None
  if not user.email().endswith('@apigee.com'):
      req.error(403)
      return None
  if checkName != None and user.email() != checkName:
      req.error(403)
      return None
  return user.email()

def getAws():
    return cloudaws.AWS(AWSKeys)

def checkAWSDomains():
    aws = getAws()
    aws.createDomain(TestDefs)
    aws.createDomain(TestRuns)
    aws.createDomain(TestResults)
    return aws

def makeItemName(userName):
    return '%s-%s' % (userName, str(datetime.datetime.utcnow()))
