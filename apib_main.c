#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include <sys/resource.h>

#include <apr_general.h>
#include <apr_getopt.h>
#include <apr_pools.h>
#include <apr_thread_proc.h>
#include <apr_time.h>

#include <apib.h>

/* Globals */

apr_pool_t* MainPool;
volatile int Running;

#define VALID_OPTS "c:d:hk:vw:"

#define VALID_OPTS_DESC \
  "[-c connections] [-k threads] [-d seconds] [-h] <url>\n" \
  "  -c: Number of connections to open (default 1)\n" \
  "  -k: Number of I/O threads to spawn (default 1)\n" \
  "  -d: Number of seconds to run (default 60)\n" \
  "  -w: Warmup time in seconds (default 0)\n" \
  "  -v: Verbose (print requests and responses)\n" \
  "  -h: Print this help message"

#define DEFAULT_NUM_CONNECTIONS 1
#define DEFAULT_NUM_THREADS 1
#define DEFAULT_LATENCIES_SIZE 1024
#define DEFAULT_DURATION 60
#define DEFAULT_WARMUP 0
#define REPORT_SLEEP_TIME 5

static void* IOThreadFunc(apr_thread_t* t, void* arg)
{
  IOArgs* args = (IOArgs*)arg;

  RunIO(args);
  return NULL;
}

static int setProcessLimits(int numConnections)
{
  struct rlimit limits;
  int err;

  getrlimit(RLIMIT_NOFILE, &limits);
  if (numConnections < limits.rlim_cur) {
    return 0;
  }
  if (numConnections < limits.rlim_max) {
    limits.rlim_cur = limits.rlim_max;
    err = setrlimit(RLIMIT_NOFILE, &limits);
    if (err == 0) {
      printf("Set file descriptor limit to %llu\n", limits.rlim_cur);
      return 0;
    } else {
      printf("Error setting file descriptor limit: %i\n", err);
      return -1;
    }
  } else {
    printf("Current hard file descriptor limit is %llu: it is too low. Try sudo.\n", 
	   limits.rlim_max);
    return -1;
  }
}

static void waitAndReport(int duration, int warmup)
{
  int durationLeft = duration;
  int toSleep;

  while (durationLeft > 0) {
    if (durationLeft < REPORT_SLEEP_TIME) {
      toSleep = durationLeft;
    } else {
      toSleep = REPORT_SLEEP_TIME;
    }
    apr_sleep(apr_time_from_sec(toSleep));
    ReportInterval(stdout, duration, warmup);
    durationLeft -= toSleep;
  }
}

int main(int ac, char const* const* av)
{
  int argc = ac;
  char const* const* argv = av;
  char const * const* env = NULL;
  apr_getopt_t* opts;
  apr_status_t s;
  char curOption;
  const char* curArg;

  /* Arguments */
  int numConnections = DEFAULT_NUM_CONNECTIONS;
  int numThreads = DEFAULT_NUM_THREADS;
  int duration = DEFAULT_DURATION;
  int warmupTime = DEFAULT_WARMUP;
  int doHelp = 0;
  int verbose = 0;
  const char* url = NULL;
  IOArgs* ioArgs;
  apr_thread_t** ioThreads;
  apr_uri_t parsedUrl;

  apr_app_initialize(&argc, &argv, &env);
  apr_pool_create(&MainPool, NULL);

  apr_getopt_init(&opts, MainPool, argc, argv);
  do {
    s = apr_getopt(opts, VALID_OPTS, &curOption, &curArg);
    if (s == APR_SUCCESS) {
      switch (curOption) {
      case 'c':
	numConnections = atoi(curArg);
	break;
      case 'd':
	duration = atoi(curArg);
	break;
      case 'h':
	doHelp = 1;
	break;
      case 'k':
	numThreads = atoi(curArg);
	break;
      case 'v':
	verbose = 1;
	break;
      case 'w':
	warmupTime = atoi(curArg);
	break;
      }
    }
  } while (s == APR_SUCCESS);

  if ((s == APR_EOF) && (opts->ind == (argc - 1))) {
    url = argv[opts->ind];
  }

  if ((s == APR_EOF) && !doHelp && (url != NULL)) {
    s = apr_uri_parse(MainPool, url, &parsedUrl);
    if (s != APR_SUCCESS) {
      fprintf(stderr, "Invalid URL\n");
      
    } else if ((parsedUrl.scheme == NULL) ||
               !(!strcmp(parsedUrl.scheme, "http") || !strcmp(parsedUrl.scheme, "https"))) {
      fprintf(stderr, "Invalid HTTP scheme\n");

    } else if (parsedUrl.hostname == NULL) {
      fprintf(stderr, "Missing host name");

    } else {

      if (setProcessLimits(numConnections) != 0) {
	return 2;
      }

      Running = 1;

      ioArgs = (IOArgs*)apr_palloc(MainPool, sizeof(IOArgs) * numThreads);
      ioThreads = (apr_thread_t**)apr_palloc(MainPool, sizeof(apr_thread_t*) * numThreads);
      for (int i = 0; i < numThreads; i++) {
	int numConn = numConnections / numThreads;
	if (i < (numConnections % numThreads)) {
	  numConn++;
	}
      
	ioArgs[i].url = &parsedUrl;
	ioArgs[i].numConnections = numConn;
	ioArgs[i].verbose = verbose;
	ioArgs[i].latenciesCount = 0;
	ioArgs[i].latenciesSize = DEFAULT_LATENCIES_SIZE;
	ioArgs[i].latencies = 
	  (unsigned long*)malloc(sizeof(unsigned long) * DEFAULT_LATENCIES_SIZE);

	apr_thread_create(&(ioThreads[i]), NULL, IOThreadFunc,
			  &(ioArgs[i]), MainPool);
      }

      if (warmupTime > 0) {
	RecordStart(FALSE);
	waitAndReport(warmupTime, TRUE);
      }

      RecordStart(TRUE);
      waitAndReport(duration, FALSE);
      RecordStop();

      Running = 0;

      for (int i = 0; i < numThreads; i++) {
	apr_status_t err;
	apr_thread_join(&err, ioThreads[i]);
      }
    }

  } else {
    fprintf(stderr, "Usage: %s %s\n", argv[0], VALID_OPTS_DESC);
  }

  ConsolidateLatencies(ioArgs, numThreads);
  PrintResults(stdout);

  apr_pool_destroy(MainPool);
  apr_terminate();

  return 0;
}
