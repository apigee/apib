from google.appengine.ext import webapp
from google.appengine.ext.webapp.util import run_wsgi_app

import pages

application = webapp.WSGIApplication([
        ('/', pages.MainPage),
        (r'/editDefinition/(.*)', pages.EditDefinition),
        ('/editDefinition', pages.EditDefinition),
        ('/updateDefinition', pages.UpdateDefinition),
        (r'/deleteDefinition/(.*)', pages.DeleteDefinition)
        ], debug=True)

def main():
    run_wsgi_app(application)

if __name__ == "__main__":
    main()
