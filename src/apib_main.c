#include <config.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <assert.h>

#include <apr_atomic.h>
#include <apr_base64.h>
#include <apr_general.h>
#include <apr_getopt.h>
#include <apr_pools.h>
#include <apr_portable.h>
#include <apr_strings.h>
#include <apr_thread_proc.h>
#include <apr_thread_rwlock.h>
#include <apr_time.h>

#include <openssl/ssl.h>
#include <openssl/crypto.h>

#if HAVE_PTHREAD_H
#include <pthread.h>
#endif

#ifndef OPENSSL_THREADS
#error This program requires OpenSSL with thread support
#endif

#include <apib.h>

/* Globals */

apr_pool_t*      MainPool;
volatile int     Running;
int              ShortOutput;
char*            RunName;
int              NumConnections;
int              NumThreads;
int              JustOnce = 0;
int              KeepAlive;

char**          Headers = NULL;
unsigned int    HeadersSize = 0;
unsigned int    NumHeaders = 0;

char*           OAuthCK = NULL;
char*           OAuthCS = NULL;
char*           OAuthAT = NULL;
char*           OAuthAS = NULL;

#if HAVE_PTHREAD_RWLOCK_INIT
pthread_rwlock_t** sslLocks = NULL;
#else
apr_thread_rwlock_t** sslLocks = NULL;
#endif

#define VALID_OPTS "c:d:f:hk:t:u:vw:x:H:O:K:M:N:X:ST1W:"

#define VALID_OPTS_DESC \
  "[-c connections] [-k keep-alive] [-K threads] [-d seconds] [-w warmup secs]\n" \
  "[-W think time (ms)] [-f file name] [-t content type] [-x verb] [-1 just once]\n" \
  "[-H HTTP Header line] [-u username:password] \n" \
  "[-N name] [-S] [-M host:port] [-X host:port] [-hv] <url>\n" \
  "  -c: Number of connections to open (default 1)\n" \
  "  -k: HTTP keep-alive setting. 0 for none, -1 for forever, otherwise seconds\n" \
  "  -K: Number of I/O threads to spawn (default 1)\n" \
  "  -d: Number of seconds to run (default 60)\n" \
  "  -w: Warmup time in seconds (default 0)\n" \
  "  -f: File to upload\n" \
  "  -t: Content type (default: application/octet-stream)\n" \
  "  -x: HTTP verb (default GET, or POST if -f is set)\n" \
  "  -O: OAuth 1.0a signature generation: see below\n" \
  "  -N: Name of test run (placed in output)\n" \
  "  -S: Short output (one line, CSV format)\n" \
  "  -T: Print header line of short output format (for CSV parsing)\n" \
  "  -M: Host and port of host running apibmon for CPU monitoring\n" \
  "  -X: Host and port of 2nd host for CPU monitoring\n" \
  "  -v: Verbose (print requests and responses)\n" \
  "  -h: Print this help message\n" \
  "\n" \
  "  if -S is used then output is CSV-separated on one line:\n" \
  "  name,throughput,avg. latency,threads,connections,duration,completed,successful,errors,sockets,min. latency,max. latency,50%,90%,98%,99%\n" \
  "\n" \
  "  if -O is used then the value is four parameters, separated by a colon:\n" \
  "  consumer key:secret:token:secret. You may omit the last two.\n"


#define DEFAULT_NUM_CONNECTIONS 1
#define DEFAULT_NUM_THREADS 1
#define DEFAULT_LATENCIES_SIZE 1024
#define DEFAULT_DURATION 60
#define DEFAULT_WARMUP 0
#define REPORT_SLEEP_TIME 5

#if HAVE_PTHREAD_CREATE
static void* IOThreadFunc(void* arg)
#else
static void* IOThreadFunc(apr_thread_t* t, void* arg)
#endif
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
      printf("Set file descriptor limit to %lu\n", limits.rlim_cur);
      return 0;
    } else {
      printf("Error setting file descriptor limit: %i\n", err);
      return -1;
    }
  } else {
    printf("Current hard file descriptor limit is %lu: it is too low. Try sudo.\n", 
	   limits.rlim_max);
    return -1;
  }
}

#if HAVE_PTHREAD_RWLOCK_INIT
static void sslLock(int mode, int n, const char* f, int l)
{
  if (mode & CRYPTO_LOCK) {
    if (mode & CRYPTO_READ) {
      pthread_rwlock_rdlock(sslLocks[n]);
    } else if (mode & CRYPTO_WRITE) {
      pthread_rwlock_wrlock(sslLocks[n]);
    }
  } else {
    pthread_rwlock_unlock(sslLocks[n]);
  }
}

static unsigned long sslThreadId(void)
{ 
#if HAVE_PTHREAD_CREATE
  return (unsigned long)pthread_self();
#else
  return apr_os_thread_current();
#endif
}

static void initSSLLocks(void)
{ 
  sslLocks = (pthread_rwlock_t**)apr_palloc(MainPool,
                sizeof(pthread_rwlock_t*) * CRYPTO_num_locks());
  for (int i = 0; i < CRYPTO_num_locks(); i++) {
    pthread_rwlock_init(sslLocks[i], NULL);
  }
  CRYPTO_set_id_callback(sslThreadId);
  CRYPTO_set_locking_callback(sslLock);
}

#else
static void sslLock(int mode, int n, const char* f, int l)
{
  if (mode & CRYPTO_LOCK) {
    if (mode & CRYPTO_READ) {
      apr_thread_rwlock_rdlock(sslLocks[n]);
    } else if (mode & CRYPTO_WRITE) {
      apr_thread_rwlock_wrlock(sslLocks[n]);
    }
  } else {
    apr_thread_rwlock_unlock(sslLocks[n]);
  }
}

static unsigned long sslThreadId(void)
{ 
  return apr_os_thread_current();
}

static void initSSLLocks(void)
{ 
  sslLocks = (apr_thread_rwlock_t**)apr_palloc(MainPool,
                sizeof(apr_thread_rwlock_t*) * CRYPTO_num_locks());
  for (int i = 0; i < CRYPTO_num_locks(); i++) {
    apr_thread_rwlock_create(&(sslLocks[i]), MainPool);
  }
  CRYPTO_set_id_callback(sslThreadId);
  CRYPTO_set_locking_callback(sslLock);
}
#endif

static void sslInfoCallback(const SSL* ssl, int where, int ret)
{
  const char* op;
  
  if (where & SSL_CB_READ) {
    op = "READ";
  } else if (where & SSL_CB_WRITE) {
    op = "WRITE";
  } else if (where & SSL_CB_HANDSHAKE_START) {
    op = "HANDSHAKE_START";
  } else if (where & SSL_CB_HANDSHAKE_DONE) {
    op = "HANDSHAKE_DONE";
  } else {
    op = "default";
  }
  printf("  ssl: op = %s ret = %i %s\n", op, ret, SSL_state_string_long(ssl));
}

static void createSslContext(IOArgs* args, int isFirst)
{
  if (isFirst) {
    SSL_load_error_strings();
    SSL_library_init();
    initSSLLocks();
  }
  args->sslCtx = SSL_CTX_new(SSLv23_client_method());
  SSL_CTX_set_options(args->sslCtx, SSL_OP_ALL);
  SSL_CTX_set_mode(args->sslCtx, SSL_MODE_ENABLE_PARTIAL_WRITE);
  if (args->verbose) {
    SSL_CTX_set_info_callback(args->sslCtx, sslInfoCallback);
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

static void cleanup(IOArgs* args)
{
  if (args == NULL) {
    return;
  }
  for (int i = 0; i < NumThreads; i++) {
    if (args[i].sslCtx != NULL) {
      /* TODO we should do this but it caused valgrind to puke if
         we already freed all the SSL handles for each connection... */
      /*      SSL_CTX_free(args[i].sslCtx); */
    }
  }
}

static void processOAuth(char* arg) 
{
  char* last;
  
  OAuthCK = apr_strtok(arg, ":", &last);
  OAuthCS = apr_strtok(NULL, ":", &last);
  OAuthAT = apr_strtok(NULL, ":", &last);
  OAuthAS = apr_strtok(NULL, ":", &last);
}

static void addHeader(char* val)
{
  if (Headers == NULL) {
    Headers = (char**)apr_palloc(MainPool, sizeof(char*));
    HeadersSize = 1;
  } else if (NumHeaders == HeadersSize) {
    HeadersSize *= 2;
    char** nh = (char**)apr_palloc(MainPool, sizeof(char*) * HeadersSize);
    memcpy(nh, Headers, sizeof(char*) * NumHeaders);
    Headers = nh;
  }
  Headers[NumHeaders] = val;
  NumHeaders++;
}

static void processBasic(const char* arg)
{
  int inLen = strlen(arg);
  int outLen = apr_base64_encode_len(inLen);
  char* out = apr_palloc(MainPool, outLen);
  char* hdr;

  apr_base64_encode(out, arg, inLen);
  hdr = apr_psprintf(MainPool, "Authorization: Basic %s", out);
  addHeader(hdr);
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
  char* monitorHost = NULL;
  char* monitor2Host = NULL;
  unsigned int thinkTime = 0;

  NumConnections = DEFAULT_NUM_CONNECTIONS;
  NumThreads = DEFAULT_NUM_THREADS;
  ShortOutput = FALSE;
  RunName = "";
  KeepAlive = KEEP_ALIVE_ALWAYS;

  IOArgs* ioArgs = NULL;
#if HAVE_PTHREAD_CREATE
  pthread_t* ioThreads;
#else
  apr_thread_t** ioThreads;
#endif
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
      case 'k':
	KeepAlive = atoi(curArg);
	break;
      case 't':
	contentType = apr_pstrdup(MainPool, curArg);
	break;
      case 'u':
        processBasic(curArg);
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
      case 'H':
        addHeader(apr_pstrdup(MainPool, curArg));
        break;
      case 'K':
	NumThreads = atoi(curArg);
	break;
      case 'M':
	monitorHost = apr_pstrdup(MainPool, curArg);
	break;
      case 'X':
	monitor2Host = apr_pstrdup(MainPool, curArg);
	break;
      case '2':
	monitor2Host = apr_pstrdup(MainPool, curArg);
	break;
      case 'N':
	RunName = apr_pstrdup(MainPool, curArg);
	break;
      case 'O':
	processOAuth(apr_pstrdup(MainPool, curArg));
	break;
      case 'S':
	ShortOutput = TRUE;
	break;
      case 'T':
	PrintReportingHeader(stdout);
	apr_terminate();
	return 0;
	break;
      case 'W':
        thinkTime = atoi(curArg);
        break;
      case '1':
	JustOnce = TRUE;
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
#if HAVE_PTHREAD_CREATE
      ioThreads = (pthread_t*)apr_palloc(MainPool, sizeof(pthread_t) * NumThreads);
#else
      ioThreads = (apr_thread_t**)apr_palloc(MainPool, sizeof(apr_thread_t*) * NumThreads);
#endif
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

        apr_pool_create(&(ioArgs[i].pool), MainPool);
	ioArgs[i].keepRunning = JustOnce;
	ioArgs[i].url = &parsedUrl;
	ioArgs[i].numConnections = numConn;
	ioArgs[i].contentType = contentType;
        ioArgs[i].headers = Headers;
        ioArgs[i].numHeaders = NumHeaders;
        ioArgs[i].delayQueue = pq_Create(ioArgs[i].pool);
	ioArgs[i].verbose = verbose;
        ioArgs[i].thinkTime = thinkTime;
	ioArgs[i].latenciesCount = 0;
	ioArgs[i].latenciesSize = DEFAULT_LATENCIES_SIZE;
	ioArgs[i].latencies = 
	  (unsigned long*)malloc(sizeof(unsigned long) * DEFAULT_LATENCIES_SIZE);
	ioArgs[i].readCount = ioArgs[i].writeCount = 0;
	ioArgs[i].readBytes = ioArgs[i].writeBytes = 0;

	if (!strcmp(parsedUrl.scheme, "https")) {
	  createSslContext(&(ioArgs[i]), (i == 0));
	} else {
	  ioArgs[i].sslCtx = NULL;
	}

#if HAVE_PTHREAD_CREATE
        pthread_create(&(ioThreads[i]), NULL, IOThreadFunc, &(ioArgs[i]));
#else
	apr_thread_create(&(ioThreads[i]), NULL, IOThreadFunc,
			  &(ioArgs[i]), MainPool);
#endif
      }

      RecordInit(monitorHost, monitor2Host);
      if (!JustOnce && (warmupTime > 0)) {
	RecordStart(FALSE);
	waitAndReport(warmupTime, TRUE);
      }

      RecordStart(TRUE);
      if (!JustOnce) {
	waitAndReport(duration, FALSE);
      }
      RecordStop();

      Running = 0;

      /* Sometimes threads don't terminate. Sleep for two seconds,
         then if a thread is stuck it won't affect the results much. */
      /*apr_sleep(apr_time_from_sec(2));  */
      for (int i = 0; i < NumThreads; i++) {
	apr_status_t err;
#if HAVE_PTHREAD_CREATE
        void* result;
        pthread_join(ioThreads[i], &result);
        pthread_detach(ioThreads[i]);
#else
	apr_thread_join(&err, ioThreads[i]);
#endif
	if (ioArgs[i].sslCtx != NULL) {
	  SSL_CTX_free(ioArgs[i].sslCtx);
	}
      }
    }

  } else {
    fprintf(stderr, "Usage: %s %s\n", argv[0], VALID_OPTS_DESC);
    apr_terminate();
    return 0;
  }

  ConsolidateLatencies(ioArgs, NumThreads);
  PrintResults(stdout);
  EndReporting();
  cleanup(ioArgs);

  apr_pool_destroy(MainPool);
  apr_terminate();

  return 0;
}
