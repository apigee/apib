import StringIO

Zones = [ 'us-east-1b', 'us-east-1c', 'us-east-1d' ]

Types = [ 'm1.large' ]

def getAmi(zone, iType):
    if zone.startswith('us-east'):
        if iType == 'm1.large':
            return 'ami-1b814f72'
    return None

def makeZoneList(name):
    str = StringIO.StringIO()
    str.write('<select name="%s">' % name)
    for z in Zones:
        str.write('<option value="%s">%s</option>' % (z, z))
    str.write('</select>')
    return str.getvalue()

def makeTypeList(name):
    str = StringIO.StringIO()
    str.write('<select name="%s">' % name)
    for z in Types:
        str.write('<option value="%s">%s</option>' % (z, z))
    str.write('</select>')
    return str.getvalue()
