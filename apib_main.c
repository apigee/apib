#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <assert.h>

#include <apr_general.h>
#include <apr_getopt.h>
#include <apr_pools.h>
#include <apr_strings.h>
#include <apr_thread_proc.h>
#include <apr_time.h>

#include <apib.h>

/* Globals */

apr_pool_t*      MainPool;
volatile int     Running;
int              ShortOutput;
char*            RunName;
int              NumConnections;
int              NumThreads;

#define VALID_OPTS "c:d:f:hk:t:vw:x:N:ST"

#define VALID_OPTS_DESC \
  "[-c connections] [-k threads] [-d seconds] [-w warmup secs]\n" \
  "[-f file name] [-t content type] [-x verb]\n" \
  "[-N name] [-S] [-hv] <url>\n" \
  "  -c: Number of connections to open (default 1)\n" \
  "  -k: Number of I/O threads to spawn (default 1)\n" \
  "  -d: Number of seconds to run (default 60)\n" \
  "  -w: Warmup time in seconds (default 0)\n" \
  "  -f: File to upload\n" \
  "  -t: Content type (default: application/octet-stream)\n" \
  "  -x: HTTP verb (default GET, or POST if -f is set)\n" \
  "  -N: Name of test run (placed in output)\n" \
  "  -S: Short output (one line, CSV format)\n" \
  "  -T: Print header line of short output format (for CSV parsing)\n" \
  "  -v: Verbose (print requests and responses)\n" \
  "  -h: Print this help message\n" \
  "\n" \
  "  if -S is used then output is CSV-separated on one line:\n" \
  "  name,throughput,avg. latency,threads,connections,duration,completed,successful,errors,sockets,min. latency,max. latency,50%,90%,98%,99%\n"


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

static int readFile(const char* name, IOArgs* args)
{
  char errBuf[80];
  apr_status_t s;
  apr_file_t* f;
  apr_finfo_t fInfo;

  s = apr_file_open(&f, name, APR_READ | APR_BINARY, APR_OS_DEFAULT, MainPool);
  if (s != APR_SUCCESS) {
    apr_strerror(s, errBuf, 80);
    fprintf(stderr, "Can't open input file %s: %i (%s)\n",
	    name, s, errBuf);
    return -1;
  }

  s = apr_file_info_get(&fInfo, APR_FINFO_SIZE, f);
  if (s != APR_SUCCESS) {
    apr_strerror(s, errBuf, 80);
    fprintf(stderr, "Can't get file info: %i (%s)\n",
	    s, errBuf);
    return -1;
  }

  args->sendDataSize = fInfo.size;
  args->sendData = (char*)apr_palloc(MainPool, sizeof(char) | fInfo.size);
  
  s = apr_file_read_full(f, args->sendData, fInfo.size, NULL);
  if (s != APR_SUCCESS) {
    apr_strerror(s, errBuf, 80);
    fprintf(stderr, "Can't read input file: %i (%s)\n",
	    s, errBuf);
    return -1;
  }

  apr_file_close(f);
  return 0;
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
  int duration = DEFAULT_DURATION;
  int warmupTime = DEFAULT_WARMUP;
  int doHelp = 0;
  int verbose = 0;
  char* verb = NULL;
  char* fileName = NULL;
  char* contentType = NULL;
  const char* url = NULL;

  NumConnections = DEFAULT_NUM_CONNECTIONS;
  NumThreads = DEFAULT_NUM_THREADS;
  ShortOutput = FALSE;
  RunName = "";

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
	NumConnections = atoi(curArg);
	break;
      case 'd':
	duration = atoi(curArg);
	break;
      case 'f':
	fileName = apr_pstrdup(MainPool, curArg);
	break;
      case 'h':
	doHelp = 1;
	break;
      case 't':
	contentType = apr_pstrdup(MainPool, curArg);
	break;
      case 'k':
	NumThreads = atoi(curArg);
	break;
      case 'v':
	verbose = 1;
	break;
      case 'w':
	warmupTime = atoi(curArg);
	break;
      case 'x':
	verb = apr_pstrdup(MainPool, curArg);
	break;
      case 'N':
	RunName = apr_pstrdup(MainPool, curArg);
	break;
      case 'S':
	ShortOutput = TRUE;
	break;
      case 'T':
	PrintReportingHeader(stdout);
	apr_terminate();
	return 0;
	break;
      default:
	assert(1);
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

      if (setProcessLimits(NumConnections) != 0) {
	return 2;
      }

      Running = 1;

      if (NumThreads > NumConnections) {
	NumThreads = NumConnections;
      }

      ioArgs = (IOArgs*)apr_palloc(MainPool, sizeof(IOArgs) * NumThreads);
      ioThreads = (apr_thread_t**)apr_palloc(MainPool, sizeof(apr_thread_t*) * NumThreads);
      for (int i = 0; i < NumThreads; i++) {
	int numConn = NumConnections / NumThreads;
	if (i < (NumConnections % NumThreads)) {
	  numConn++;
	}
      
	if (fileName != NULL) {
	  if (readFile(fileName, &(ioArgs[i])) != 0) {
	    return 3;
	  }
	} else {
	  ioArgs[i].sendData = NULL;
	  ioArgs[i].sendDataSize = 0;
	}

	if (verb == NULL) {
	  if (fileName == NULL) {
	    ioArgs[i].httpVerb = "GET";
	  } else {
	    ioArgs[i].httpVerb = "POST";
	  } 
	} else {
	  ioArgs[i].httpVerb = verb;
	}

	ioArgs[i].url = &parsedUrl;
	ioArgs[i].numConnections = numConn;
	ioArgs[i].contentType = contentType;
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

      for (int i = 0; i < NumThreads; i++) {
	apr_status_t err;
	apr_thread_join(&err, ioThreads[i]);
      }
    }

  } else {
    fprintf(stderr, "Usage: %s %s\n", argv[0], VALID_OPTS_DESC);
    apr_terminate();
    return 0;
  }

  ConsolidateLatencies(ioArgs, NumThreads);
  PrintResults(stdout);

  apr_pool_destroy(MainPool);
  apr_terminate();

  return 0;
}
