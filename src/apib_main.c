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
int             HostHeaderOverride = 0;

char*           OAuthCK = NULL;
char*           OAuthCS = NULL;
char*           OAuthAT = NULL;
char*           OAuthAS = NULL;

#if HAVE_PTHREAD_RWLOCK_INIT
pthread_rwlock_t* sslLocks = NULL;
#else
apr_thread_rwlock_t** sslLocks = NULL;
#endif

static apr_getopt_option_t Options[] =
  { 
    { "concurrency", 'c', 1, "Number of concurrent requests (default 1)" },
    { "duration", 'd', 1, "Test duration in seconds" },
    { "input-file", 'f', 1, "File name to send on PUT and POST requests" },
    { "help", 'h', 0, "Display this message" },
    { "keep-alive", 'k', 1, "Keep-alive duration -- 0 to disable, non-zero for timeout (default 9999)" },
    { "content-type", 't', 1, "Value of the Content-Type header" },
    { "username-password", 'u', 1, "Credentials for HTTP Basic authentication in username:password format" },
    { "verbose", 'v', 0, "Verbose output" },
    { "warmup", 'w', 1, "Warm-up duration, in seconds (default 0)" },
    { "method", 'x', 1, "HTTP request method (default GET)" },
    { "header", 'H', 1, "HTTP header line in Name: Value format" },
    { "oauth", 'O', 1, "OAuth 1.0 signature, in format consumerkey:secret:token:secret" },
    { "iothreads", 'K', 1, "Number of I/O threads to spawn, default == number of CPU cores" },
    { "monitor", 'M', 1, "Host name and port number of host running apibmon" },
    { "monitor2", 'X', 1, "Second host name and port number of host running apibmon" },
    { "name", 'N', 1, "Name to put in CSV output to identify test run" },
    { "csv-output", 'S', 0, "Output all test results in a single CSV line" },
    { "header-line", 'T', 0, "Do not run, but output a single CSV header line" },
    { "one", '1', 0, "Send just one request and exit" },
    { "think-time", 'W', 1, "Think time to wait in between requests, in milliseconds" },
    { NULL, 0, 0, NULL }
  };

#define USAGE_DOCS \
  "\n" \
  "The last argument may be an http or https URL, or an \"@\" symbol followed\n" \
  "by a file name. If a file name, then apib will read the file as a list of\n" \
  "URLs, one per line, and randoml Test each one.\n" \
  "\n" \
  "  if -S is used then output is CSV-separated on one line:\n" \
  "  name,throughput,avg. latency,threads,connections,duration,completed,successful,errors,sockets,min. latency,max. latency,50%,90%,98%,99%\n" \
  "\n" \
  "  if -O is used then the value is four parameters, separated by a colon:\n" \
  "  consumer key:secret:token:secret. You may omit the last two.\n"

#define DEFAULT_NUM_CONNECTIONS 1
#define DEFAULT_LATENCIES_SIZE 1024
#define DEFAULT_DURATION 60
#define DEFAULT_WARMUP 0
#define REPORT_SLEEP_TIME 5

static void printUsage(void)
{
  fprintf(stderr, "Usage: apib [options] [URL | @file]\n");
  int i = 0;
  while (Options[i].optch != 0) {
    apr_getopt_option_t* opt = &(Options[i]);
    fprintf(stderr, "-%c\t%s\t%s\n",
	    opt->optch, opt->name, opt->description);
    i++;
  }
  fprintf(stderr, "%s", USAGE_DOCS);
}

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
      return 0;
    } else {
      fprintf(stderr, "Error setting file descriptor limit: %i\n", err);
      return -1;
    }
  } else {
    fprintf(stderr, "Current hard file descriptor limit is %lu: it is too low. Try sudo.\n", 
	   limits.rlim_max);
    return -1;
  }
}

#if HAVE_PTHREAD_RWLOCK_INIT
static void sslLock(int mode, int n, const char* f, int l)
{
  if (mode & CRYPTO_LOCK) {
    if (mode & CRYPTO_READ) {
      pthread_rwlock_rdlock(&(sslLocks[n]));
    } else if (mode & CRYPTO_WRITE) {
      pthread_rwlock_wrlock(&(sslLocks[n]));
    }
  } else {
    pthread_rwlock_unlock(&(sslLocks[n]));
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
  sslLocks = (pthread_rwlock_t*)apr_pcalloc(MainPool,
                sizeof(pthread_rwlock_t) * CRYPTO_num_locks());
  for (int i = 0; i < CRYPTO_num_locks(); i++) {
    pthread_rwlock_init(&(sslLocks[i]), NULL);
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
  char* tokLast;
  char* tok;

  if (Headers == NULL) {
    Headers = (char**)apr_palloc(MainPool, sizeof(char*));
    HeadersSize = 1;
  } else if (NumHeaders == HeadersSize) {
    HeadersSize *= 2;
    char** nh = (char**)apr_palloc(MainPool, sizeof(char*) * HeadersSize);
    memcpy(nh, Headers, sizeof(char*) * NumHeaders);
    Headers = nh;
  }
  Headers[NumHeaders] = apr_pstrdup(MainPool, val);
  NumHeaders++;

  tok = apr_strtok(val, ":", &tokLast);
  if ((tok != NULL) && !strcmp("Host", tok)) {
    HostHeaderOverride = 1;
  }
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
  int curOption;
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
  NumThreads = -1;
  ShortOutput = FALSE;
  RunName = "";
  KeepAlive = KEEP_ALIVE_ALWAYS;

  IOArgs* ioArgs = NULL;
#if HAVE_PTHREAD_CREATE
  pthread_t* ioThreads;
#else
  apr_thread_t** ioThreads;
#endif

  apr_app_initialize(&argc, &argv, &env);
  apr_pool_create(&MainPool, NULL);

  apr_getopt_init(&opts, MainPool, argc, argv);
  do {
    s = apr_getopt_long(opts, Options, &curOption, &curArg);
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
    if (url[0] == '@') {
      if (url_InitFile(url + 1, MainPool) != 0) {
	fprintf(stderr, "Invalid URL file\n");
	goto finished;
      }
    } else {
      if (url_InitOne(url, MainPool) != 0) {
	fprintf(stderr, "Invalid URL\n");
	goto finished;
      }
    }

    if (setProcessLimits(NumConnections) != 0) {
      goto finished;
    }

    Running = 1;

    if (NumThreads < 1) {
      NumThreads = cpu_Count(MainPool);
    }
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
      ioArgs[i].numConnections = numConn;
      ioArgs[i].contentType = contentType;
      ioArgs[i].headers = Headers;
      ioArgs[i].numHeaders = NumHeaders;
      ioArgs[i].hostHeaderOverride = HostHeaderOverride;
      ioArgs[i].delayQueue = pq_Create(ioArgs[i].pool);
      ioArgs[i].verbose = verbose;
      ioArgs[i].thinkTime = thinkTime;
      ioArgs[i].latenciesCount = 0;
      ioArgs[i].latenciesSize = DEFAULT_LATENCIES_SIZE;
      ioArgs[i].latencies = 
	(unsigned long*)malloc(sizeof(unsigned long) * DEFAULT_LATENCIES_SIZE);
      ioArgs[i].readCount = ioArgs[i].writeCount = 0;
      ioArgs[i].readBytes = ioArgs[i].writeBytes = 0;

      createSslContext(&(ioArgs[i]), (i == 0));

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
#if HAVE_PTHREAD_CREATE
      void* result;
      pthread_join(ioThreads[i], &result);
      pthread_detach(ioThreads[i]);
#else
      apr_status_t err;
      apr_thread_join(&err, ioThreads[i]);
#endif
      if (ioArgs[i].sslCtx != NULL) {
	SSL_CTX_free(ioArgs[i].sslCtx);
      }
    }

  } else {
    printUsage();
    apr_terminate();
    return 0;
  }

  ConsolidateLatencies(ioArgs, NumThreads);
  PrintResults(stdout);
  EndReporting();
  cleanup(ioArgs);

finished:
  apr_pool_destroy(MainPool);
  apr_terminate();

  return 0;
}
