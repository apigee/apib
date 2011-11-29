from google.appengine.api import users
from google.appengine.ext import webapp

import urllib

import awsenv
import cloudaws
import utils

class CreateRun(webapp.RequestHandler):
    createForm = \
      '<html><head><title>Create Test Run</title></head><body>' \
      '<form action="/confirmRun" method="post">' \
      '<label>Name: </label>' \
      '<input type="text" size="40" name="name"/><br/>' \
      '<label>Availability Zone: </label>%s' \
      '<label>Instance Type: </label>%s' \
      '<input type="submit"/>' \
      '<input type="hidden" name="itemName" value="%s"/>' \
      '<input type="hidden" name="userName" value="%s"/>' \
      '</form></body></html>'

    def get(self, iName):
        itemName = urllib.unquote(iName)
        aws = utils.getAws()
        testDef = aws.get(utils.TestDefs, itemName);
        userName = utils.checkUser(self, testDef['userName'])
        if userName == None:
            return
        self.response.out.write(self.createForm % \
            (awsenv.makeZoneList('availability_zone'), 
             awsenv.makeTypeList('instance_type'), 
             itemName, userName))

class ConfirmRun(webapp.RequestHandler):
      confirmForm = \
      '<html><head><title>Confirm Test Run</title></head><body>' \
      '<p>For %s</p>' \
      '<form action="/launchTest" method="post">' \
      '<label>Name: %s</label></s>' \
      '<input type="hidden" name="name" value="%s"/><br/>' \
      '<label>Availability Zone: %s</label>' \
      '<input type="hidden" name="availability_zone" value="%s"/><br/>' \
      '<label>Instance Type: %s</label>' \
      '<input type="hidden" name="instance_type" value="%s"/><br/>' \
      '<input type="submit"/><input type="reset"/>' \
      '<input type="hidden" name="itemName" value="%s"/>' \
      '<input type="hidden" name="userName" value="%s"/>' \
      '</form></body></html>'

      def post(self):
        userName = utils.checkUser(self, self.request.get('userName'))
        if userName == None:
            return
        self.response.out.write(self.confirmForm % \
          (userName,
           self.request.get('name'), self.request.get('name'),
           self.request.get('availability_zone'), self.request.get('availability_zone'),
           self.request.get('instance_type'), self.request.get('instance_type'),
           self.request.get('itemName'), userName))

class LaunchTest(webapp.RequestHandler):
      def post(self):
          userName = utils.checkUser(self, self.request.get('userName'))
          if userName == None:
              return
          runName = utils.makeItemName(userName)
          defName = self.request.get('itemName')
          zone = self.request.get('availability_zone')
          iType = self.request.get('instance_type')
          runInfo = {
              'name' : self.request.get('name'),
              'defName' : defName,
              'userName' : userName,
              'availability_zone' : zone,
              'instance_type' : iType,
              }
          aws = utils.getAws()
          aws.put(utils.TestRuns, runName, runInfo)

          iData = '%s,%s' % (runName, defName)
          
          # TODO Greg make it so!
          aws.launchInstance(awsenv.getAmi(zone, iType), zone, iType, iData)
          self.redirect('/')

def getConcurrency(o):
    return o[1]['concurrency']

class ViewTest(webapp.RequestHandler):
    def get(self, iName):
        itemName = urllib.unquote(iName)
        userName = utils.checkUser(self)
        if userName == None:
            return
        aws = utils.getAws()
        results = aws.select('select * from %s where runName = "%s"' %
                      (utils.TestResults, itemName))

        sro = self.response.out
        sro.write('<html><head><title>Test Result</title></head><body>')
        sro.write('<h1>Test Results</h1>')
        sro.write('<p><b><a href="/">Back</a></b></p>')
        sro.write('<table border="1">')
        sro.write('<tr><td><b>Concurrency</b></td><td><b>Throughput</b></td>\
                       <td><b>Errors</b></td><td><b>Average Latency</b></td>\
                       <td><b>Median Latency</b></td></tr>')
        rItems = results.items()
        rItems.sort(key=getConcurrency)
        for ro in rItems:
            r = ro[1]
            sro.write('<tr><td><a href="/viewTestDetails/%s">%s</a></td><td>%s</td><td>%s</td><td>%s</td><td>%s</td></tr>' % \
                       (ro[0], r['concurrency'], r['throughput'], r['errors'],
                        r['avg_latency'], r['latency_50']))
        sro.write('</table>')
        sro.write('<p><b><a href="/">Back</a></b></p>')
        sro.write('</body></html>')

def getKey(o):
    return o[0]

class ViewTestDetails(webapp.RequestHandler):
    def get(self, dName=''):
        detailName = urllib.unquote(dName)
        userName = utils.checkUser(self)
        if userName == None:
            return
        aws = utils.getAws()
        details = aws.get(utils.TestResults, detailName)

        sro = self.response.out
        sro.write('<html><head><title>Test Details</title></head><body>')
        sro.write('<h1>Test Details</h1>')
        sro.write('<p><b><a href="/">Back</a></b></p>')
        sro.write('<table border="1">')
        di = details.items()
        di.sort(key=getKey)
        for r in di:
            sro.write('<tr><td>%s</td><td>%s</td></tr>' % r)
        sro.write('</table>')
        sro.write('<p><b><a href="/">Back</a></b></p>')
        sro.write('</body></html>')

class DeleteTest(webapp.RequestHandler):
    def get(self, iName=''):
        itemName = urllib.unquote(iName)
        userName = utils.checkUser(self)
        if userName == None:
            return
        aws = utils.getAws()
        results = aws.select('select * from %s where runName = "%s"' %
                      (utils.TestResults, itemName))

        for r in results.items():
            aws.delete(utils.TestResults, r[0])
        aws.delete(utils.TestRuns, itemName)
        self.redirect('/')

