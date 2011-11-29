from google.appengine.api import users
from google.appengine.ext import webapp

import datetime
import urllib

import cloudaws
import utils

class MainPage(webapp.RequestHandler):

    pageHeader = \
     '<html><head><title>Test Launcher</title></head> \
      <body><h1>Test Launcher</h1> \
      <p>%s</p> \
      <h2>Test Definitions</h2>'
    pageFooter = \
     '</body></html>'

    def get(self):
        userName = utils.checkUser(self)
        if userName == None:
            return
        aws = utils.checkAWSDomains()
        sro = self.response.out
        sro.write(self.pageHeader % userName)
        defs = aws.select(
            "select * from %s where userName = '%s'" % (utils.TestDefs, userName))
        for d in defs.items():
            sro.write(
              '<p><a href="/editDefinition/%s">%s</a>\
                  <a href="/createRun/%s"> (Launch test) </a>\
                  <a href="/deleteDefinition/%s"> (delete) </a></p>' % \
                (d[0], d[1]['name'], d[0], d[0]))
        sro.write('<p><a href="/editDefinition">Add Definition</a></p>')
        sro.write('<h2>Test Runs</h2>')
        runs = aws.select(
            "select * from %s where userName = '%s'" % (utils.TestRuns, userName))
        for r in runs.items():
            sro.write('<p><a href="/viewTest/%s">%s</a>\
                          <a href="/deleteTest/%s"> (delete)</a></p>' % \
                (r[0], r[1]['name'], r[0]))
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
      '<input type="hidden" name="userName" value="%s"/>' \
      '</form></body></html>'

    def get(self, iName=''):
        userName = utils.checkUser(self)
        if userName == None:
            return
        aws = utils.getAws()
        testDef = None
        if iName != '':
            itemName = urllib.unquote(iName)
            testDef = aws.get(utils.TestDefs, itemName)
        if testDef == None:
            testDef = {
                'itemName' : '',
                'name' : '',
                'url' : '',
                'duration' : '3',
                'max_concurrency' : '100',
                'think_time' : '0',
                'userName': userName
                }
        else:
            testDef['itemName'] = itemName
            testDef['userName'] = userName

        self.response.out.write(self.editForm % \
            (testDef['name'], testDef['url'], testDef['duration'],
             testDef['think_time'], testDef['max_concurrency'],
             testDef['itemName'], testDef['userName']))

class UpdateDefinition(webapp.RequestHandler):
    def post(self):
        userName = utils.checkUser(self, self.request.get('userName'))
        if userName == None:
            return
        
        record = {
            'name' : self.request.get('name'),
            'url' : self.request.get('url'),
            'duration' : self.request.get('duration'),
            'max_concurrency' : self.request.get('max_concurrency'),
            'think_time' : self.request.get('think_time'),
            'userName' : userName
            }
        itemName = self.request.get('itemName')
        if itemName == None or itemName == '':
            itemName = utils.makeItemName(userName)
        aws = utils.getAws()
        aws.put(utils.TestDefs, itemName, record)
        self.redirect('/')

class DeleteDefinition(webapp.RequestHandler):
    def get(self, iName=''):
        user = utils.checkUser(self)
        if user == None:
            return
        itemName = urllib.unquote(iName)
        aws = utils.getAws()
        aws.delete(utils.TestDefs, itemName)
        self.redirect('/')


