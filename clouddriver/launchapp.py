from google.appengine.ext import webapp
from google.appengine.ext.webapp.util import run_wsgi_app

import pages
import runs

application = webapp.WSGIApplication([
        ('/', pages.MainPage),
        (r'/editDefinition/(.*)', pages.EditDefinition),
        ('/editDefinition', pages.EditDefinition),
        ('/updateDefinition', pages.UpdateDefinition),
        (r'/deleteDefinition/(.*)', pages.DeleteDefinition),
        (r'/createRun/(.*)', runs.CreateRun),
        ('/confirmRun', runs.ConfirmRun),
        ('/launchTest', runs.LaunchTest),
        (r'/viewTest/(.*)', runs.ViewTest),
        (r'/viewTestDetails/(.*)', runs.ViewTestDetails),
        (r'/deleteTest/(.*)', runs.DeleteTest)
        ], debug=True)

def main():
    run_wsgi_app(application)

if __name__ == "__main__":
    main()
