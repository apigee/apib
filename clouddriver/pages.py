from google.appengine.api import users
from google.appengine.ext import webapp

import datetime
import urllib

import cloudaws

TestDefs='TestDefs'
TestRuns='TestRuns'
AWSKeys='aws.keys'

def checkUser(req):
  user = users.get_current_user()
  if user == None:
      req.error(403)
      return None
  if not user.email().endswith('@apigee.com'):
      req.error(403)
      return None
  return user.email()

def checkAWSDomains():
    aws = cloudaws.AWS(AWSKeys)
    aws.createDomain(TestDefs)
    aws.createDomain(TestRuns)
    return aws

def makeItemName(userName):
    return '%s-%s' % (userName, str(datetime.datetime.utcnow()))

class MainPage(webapp.RequestHandler):

    pageHeader = \
     '<html><head><title>Test Launcher</title></head> \
      <body><h1>Test Launcher</h1> \
      <p>%s</p> \
      <h2>Test Definitions</h2>'
    pageFooter = \
     '</body></html>'

    def get(self):
        userName = checkUser(self)
        if userName == None:
            return
        aws = checkAWSDomains()
        sro = self.response.out
        sro.write(self.pageHeader % userName)
        defs = aws.select(
            "select * from %s where userName = '%s'" % (TestDefs, userName))
        sro.write('TODO Greg: Put a run button next to each. Open form to select details')
        for d in defs.items():
            sro.write('<p><a href="/editDefinition/%s">%s</a> <a href="/deleteDefinition/%s">(delete)</a></p>' % \
                (d[0], d[1]['name'], d[0]))
        sro.write('<p><a href="/editDefinition">Add Definition</a></p>')
        sro.write('<h2>Test Runs</h2>')
        sro.write('<p>TODO Greg list existing runs here, allow delete</p>')
        runs = aws.select(
            "select itemName(), name from %s where userName = '%s'" % (TestRuns, userName))
        for r in runs.values():
            sro.write('<p><a href="foobar">%s</a></p>' % d['name'])
        sro.write(self.pageFooter)

class EditDefinition(webapp.RequestHandler):
    
    editForm = \
      '<html><head><title>Edit Test Definition</title></head><body>' \
      '<form action="/updateDefinition" method="post">' \
      '<label>Name: </label>' \
      '<input type="text" size="40" name="name" value="%s"/><br/>' \
      '<label>URL: </label>' \
      '<input type="text" size="100" name="url" value="%s"/><br/>' \
      '<label>Test Duration (minutes): </label>' \
      '<input type="text" size="40" name="duration" value="%s"/><br/>' \
      '<label>Think Time (milliseconds): </label>' \
      '<input type="text" size="40" name="think_time" value="%s"/><br/>' \
      '<label>Max Concurrency: </label>' \
      '<input type="text" size="40" name="max_concurrency" value="%s"/><br/>' \
      '<input type="submit"/>' \
      '<input type="hidden" name="itemName" value="%s"/>' \
      '</form></body></html>'

    def get(self, iName=''):
        aws = cloudaws.AWS(AWSKeys)
        testDef = None
        if iName != '':
            itemName = urllib.unquote(iName)
            testDef = aws.get(TestDefs, itemName)
        if testDef == None:
            testDef = {
                'itemName' : '',
                'name' : '',
                'url' : '',
                'duration' : '3',
                'max_concurrency' : '100',
                'think_time' : '0'
                }
        else:
            testDef['itemName'] = itemName

        self.response.out.write(self.editForm % \
            (testDef['name'], testDef['url'], testDef['duration'],
             testDef['max_concurrency'], testDef['think_time'],
             testDef['itemName']))

class UpdateDefinition(webapp.RequestHandler):
    def post(self):
        userName = checkUser(self)
        if userName == None:
            return
        record = {
            'userName' : userName,
            'name' : self.request.get('name'),
            'url' : self.request.get('url'),
            'duration' : self.request.get('duration'),
            'max_concurrency' : self.request.get('max_concurrency'),
            'think_time' : self.request.get('think_time')
            }
        itemName = self.request.get('itemName')
        if itemName == None or itemName == '':
            itemName = makeItemName(userName)
        aws = cloudaws.AWS(AWSKeys)
        aws.put(TestDefs, itemName, record)
        self.redirect('/')

class DeleteDefinition(webapp.RequestHandler):
    def get(self, iName=''):
        itemName = urllib.unquote(iName)
        aws = cloudaws.AWS(AWSKeys)
        aws.delete(TestDefs, itemName)
        self.redirect('/')

