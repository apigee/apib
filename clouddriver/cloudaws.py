import base64
import ConfigParser
import datetime
import hashlib
import httplib
import hmac
import StringIO
import urllib
import urlparse
import xml.dom
import xml.dom.minidom

Debug  = False

def getElementText(n):
    c = n.firstChild
    str = StringIO.StringIO()
    while c != None:
        if c.nodeType == xml.dom.Node.TEXT_NODE:
            str.write(c.data)
        c = c.nextSibling
    return str.getvalue()

class AWS:
    AWSVersion = '2009-04-15'
    AWSSignatureVersion = '2'
    AWSHost = 'sdb.amazonaws.com'
    AWSBaseURI = 'http://sdb.amazonaws.com/'
    EC2Version = '2011-11-01'
    EC2Host = 'ec2.amazonaws.com'
    EC2BaseURI = 'http://ec2.amazonaws.com/'
    EC2KeyPair = 'auto-perf-test'
    EC2SecurityGroup = 'auto-perf-test'

    def __init__(self, keyFile):
        cp = ConfigParser.ConfigParser()
        cp.read([keyFile])
        self.AWSKey = cp.get('AWS', 'key')
        self.AWSSecret = cp.get('AWS', 'secret')
        
    def calculateSignature(self, verb, uriStr, query):
        now = datetime.datetime.utcnow()
        query['AWSAccessKeyId'] = self.AWSKey
        query['SignatureVersion'] = self.AWSSignatureVersion
        query['SignatureMethod'] = 'HmacSHA256'
        query['Timestamp'] = now.strftime('%Y-%m-%dT%H:%M:%SZ')

        uri = urlparse.urlparse(uriStr)
        sig = StringIO.StringIO()
        sig.write('%s\n' % verb)
        sig.write('%s\n' % uri[1])
        if (uri[2] == None) or (uri[2] == ''):
            sig.write('/\n')
        else:
            sig.write('%s\n' % uri[2])
        if query != None:
            qKeys = query.keys()
            qKeys.sort()
            once = False
            for k in qKeys:
                if once:
                    sig.write('&')
                else:
                    once = True
                if k in qKeys:
                    sig.write('%s=%s' % (urllib.quote(k, ''), urllib.quote(str(query[k]), '')))
                else:
                    sig.write('%s=' % urllib.quote(k, ''))
        base = sig.getvalue()

        if Debug:
            print 'Base: \"%s\"\n' % base

        mac = hmac.new(self.AWSSecret, base, hashlib.sha256)
        sig = mac.digest()
        query['Signature'] = base64.b64encode(sig)

    def makeParams(self, qps):
        buf = StringIO.StringIO()
        once = False
        for i in qps.items():
            if once:
                buf.write('&')
            else:
                once = True
            if i[1] != None:
                buf.write('%s=%s' % (urllib.quote(i[0]), urllib.quote(str(i[1]))))
            else:
                buf.write('%s=' % urllib.quote(i[0]))
        return buf.getvalue()

    def doGenericPost(self, qps, uri, host, version):
        qps['Version'] = version
        self.calculateSignature('POST', uri, qps)
        conn = httplib.HTTPConnection(host)
        if Debug:
            print 'Sending ', self.makeParams(qps)
        conn.request('POST', uri, self.makeParams(qps), 
                     {'Content-Type' : 'application/x-www-form-urlencoded'})
        resp = conn.getresponse()
        return resp

    def doPost(self, qps):
        return self.doGenericPost(qps, self.AWSBaseURI, self.AWSHost, self.AWSVersion)

    def doGet(self, qps):
        qps['Version'] = self.AWSVersion
        self.calculateSignature('GET', self.AWSBaseURI, qps)
        conn = httplib.HTTPConnection(self.AWSHost)
        uri = '%s?%s' % (self.AWSBaseURI, self.makeParams(qps))
        if Debug:
            print 'Sending ', uri
        conn.request('GET', uri)
        resp = conn.getresponse()
        return resp

    def put(self, domain, item, attrs):
        qps = dict()
        qps['ItemName'] = item
        qps['DomainName'] = domain
        qps['Action'] = 'PutAttributes'
        if attrs != None:
            inc = 0
            for i in attrs.items():
                qps['Attribute.%i.Name' % inc] = i[0]
                qps['Attribute.%i.Value' % inc] = i[1]
                inc = inc + 1
        self.doPost(qps)
    
    def get(self, domain, item):
        qps = dict()
        qps['ItemName'] = item
        qps['DomainName'] = domain
        qps['Action'] = 'GetAttributes'

        doc = xml.dom.minidom.parse(self.doGet(qps))
        attrs = doc.getElementsByTagName('Attribute')
        result = dict()
        for attr in attrs:
            ac = attr.firstChild
            while ac != None:
                if ac.nodeName == 'Name':
                    nam = getElementText(ac)
                elif ac.nodeName == 'Value':
                    val = getElementText(ac)
                ac = ac.nextSibling
            result[nam] = val
        return result
            
    def delete(self, domain, item):
        qps = { 'ItemName' : item, 'DomainName' : domain,
                'Action' : 'DeleteAttributes' }
        self.doPost(qps)

    def createDomain(self, domain):
        qps = dict()
        qps['DomainName'] = domain
        qps['Action'] = 'CreateDomain'
        self.doPost(qps)
        
    def deleteDomain(self, domain):
        qps = {'Action' : 'DeleteDomain',
               'DomainName' : domain}
        self.doPost(qps)

    def select(self, expression):
        qps = {'Action' : 'Select',
               'SelectExpression' : expression }
        doc = xml.dom.minidom.parse(self.doGet(qps))
        items = doc.getElementsByTagName('Item')
        result = dict()
        for item in items:
            attrs = dict()
            c = item.firstChild
            while c != None:
                if c.nodeName == 'Name':
                    itemName = getElementText(c)
                elif c.nodeName == 'Attribute':
                    ac = c.firstChild
                    while ac != None:
                        if ac.nodeName == 'Name':
                            nam = getElementText(ac)
                        elif ac.nodeName == 'Value':
                            val = getElementText(ac)
                        ac = ac.nextSibling
                    attrs[nam] = val
                c = c.nextSibling
            result[itemName] = attrs
        return result

    def launchInstance(self, ami, zone, iType, metaData):
        qps = {'Action' : 'RunInstances',
               'ImageId' : ami,
               'MinCount' : '1', 'MaxCount' : '1',
               'KeyName' : self.EC2KeyPair,
               'SecurityGroup.0' : self.EC2SecurityGroup,
               'InstanceType' : iType,
               'Placement.AvailabilityZone' : zone,
               'UserData' : base64.encodestring(metaData),
               'DisableApiTermination' : 'false',
               'InstanceInitiatedShutdownBehavior' : 'terminate'
               }
               
        result = self.doGenericPost(qps, self.EC2BaseURI, self.EC2Host, self.EC2Version)
        print result.read()
