/*
Copyright 2019 Google LLC

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    https://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
*/

#include <assert.h>
#include <fcntl.h>
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>

#include "src/apib_cpu.h"
#include "src/apib_iothread.h"
#include "src/apib_reporting.h"
#include "src/apib_url.h"

#define DEFAULT_CONTENT_TYPE "application/octet-stream"
#define KEEP_ALIVE_ALWAYS -1
#define KEEP_ALIVE_NEVER 0

/* Globals */
volatile int Running;
int ShortOutput;
char* RunName;
int NumConnections;
int NumThreads;
int JustOnce = 0;
int KeepAlive;

char** Headers = NULL;
unsigned int HeadersSize = 0;
unsigned int NumHeaders = 0;
int HostHeaderOverride = 0;

char* OAuthCK = NULL;
char* OAuthCS = NULL;
char* OAuthAT = NULL;
char* OAuthAS = NULL;

#define OPTIONS "c:d:f:hk:t:u:vw:x:C:H:O:K:M:X:N:ST1W:"

static const struct option Options[] = {
    {"concurrency", required_argument, NULL, 'c'},
    {"duration", required_argument, NULL, 'd'},
    {"input-file", required_argument, NULL, 'f'},
    {"help", no_argument, NULL, 'h'},
    {"keep-alive", required_argument, NULL, 'k'},
    {"content-type", required_argument, NULL, 't'},
    {"username-password", required_argument, NULL, 'u'},
    {"verbose", no_argument, NULL, 'v'},
    {"warmup", required_argument, NULL, 'w'},
    {"method", required_argument, NULL, 'x'},
    {"cipherlist", required_argument, NULL, 'C'},
    {"header", required_argument, NULL, 'H'},
    {"oauth", required_argument, NULL, 'O'},
    {"iothreads", required_argument, NULL, 'K'},
    {"monitor", required_argument, NULL, 'M'},
    {"monitor2", required_argument, NULL, 'X'},
    {"name", required_argument, NULL, 'N'},
    {"csv-output", no_argument, NULL, 'S'},
    {"header-line", no_argument, NULL, 'T'},
    {"one", no_argument, NULL, '1'},
    {"think-time", required_argument, NULL, 'W'}};

#define USAGE_DOCS                                                             \
  "-1 --one                Send just one request and exit\n"                   \
  "-c --concurrency        Number of concurrent requests (default 1)\n"        \
  "-d --duration           Test duration in seconds\n"                         \
  "-f --input-file         File name to send on PUT and POST requests\n"       \
  "-h --help               Display this message\n"                             \
  "-k --keep-alive         Keep-alive duration -- 0 to disable, non-zero for " \
  "timeout\n"                                                                  \
  "-t --content-type       Value of the Content-Type header\n"                 \
  "-u --username-password  Credentials for HTTP Basic authentication in "      \
  "username:password format\n"                                                 \
  "-v --verbose            Verbose output\n"                                   \
  "-w --warmup             Warm-up duration, in seconds (default 0)\n"         \
  "-x --method             HTTP request method (default GET)\n"                \
  "-C --cipherlist         Cipher list offered to server for HTTPS\n"          \
  "-H --header             HTTP header line in Name: Value format\n"           \
  "-K --iothreads          Number of I/O threads to spawn, default == number " \
  "of CPU cores\n"                                                             \
  "-N --name               Name to put in CSV output to identify test run\n"   \
  "-O --oauth              OAuth 1.0 signature, in format "                    \
  "consumerkey:secret:token:secret\n"                                          \
  "-S --csv-output         Output all test results in a single CSV line\n"     \
  "-T --header-line        Do not run, but output a single CSV header line\n"  \
  "-W --think-time         Think time to wait in between requests, in "        \
  "milliseconds\n"                                                             \
  "-M --monitor            Host name and port number of host running "         \
  "apibmon\n"                                                                  \
  "-X --monitor2           Second host name and port number of host running "  \
  "apibmon\n"                                                                  \
  "\n"                                                                         \
  "The last argument may be an http or https URL, or an \"@\" symbol "         \
  "followed\n"                                                                 \
  "by a file name. If a file name, then apib will read the file as a list "    \
  "of\n"                                                                       \
  "URLs, one per line, and randoml Test each one.\n"                           \
  "\n"                                                                         \
  "  if -S is used then output is CSV-separated on one line:\n"                \
  "  name,throughput,avg. "                                                    \
  "latency,threads,connections,duration,completed,successful,errors,sockets,"  \
  "min. latency,max. latency,50%,90%,98%,99%\n"                                \
  "\n"                                                                         \
  "  if -O is used then the value is four parameters, separated by a colon:\n" \
  "  consumer key:secret:token:secret. You may omit the last two.\n"
#define DEFAULT_NUM_CONNECTIONS 1
#define DEFAULT_LATENCIES_SIZE 1024
#define DEFAULT_DURATION 60
#define DEFAULT_WARMUP 0
#define REPORT_SLEEP_TIME 5

static void printUsage() {
  fprintf(stderr, "Usage: apib [options] [URL | @file]\n");
  fprintf(stderr, "%s", USAGE_DOCS);
}

static int setProcessLimits(int numConnections) {
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
    fprintf(stderr,
            "Current hard file descriptor limit is %llu: it is too low. Try "
            "sudo.\n",
            limits.rlim_max);
    return -1;
  }
}

#if 0
#if HAVE_PTHREAD_RWLOCK_INIT
static void sslLock(int mode, int n, const char* f, int l) {
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

static unsigned long sslThreadId(void) {
#if HAVE_PTHREAD_CREATE
  return (unsigned long)pthread_self();
#else
  return apr_os_thread_current();
#endif
}

static void initSSLLocks(void) {
  sslLocks = (pthread_rwlock_t*)apr_pcalloc(
      MainPool, sizeof(pthread_rwlock_t) * CRYPTO_num_locks());
  for (int i = 0; i < CRYPTO_num_locks(); i++) {
    pthread_rwlock_init(&(sslLocks[i]), NULL);
  }
  CRYPTO_set_id_callback(sslThreadId);
  CRYPTO_set_locking_callback(sslLock);
}

#else
static void sslLock(int mode, int n, const char* f, int l) {
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

static unsigned long sslThreadId(void) { return apr_os_thread_current(); }

static void initSSLLocks(void) {
  sslLocks = (apr_thread_rwlock_t**)apr_palloc(
      MainPool, sizeof(apr_thread_rwlock_t*) * CRYPTO_num_locks());
  for (int i = 0; i < CRYPTO_num_locks(); i++) {
    apr_thread_rwlock_create(&(sslLocks[i]), MainPool);
  }
  CRYPTO_set_id_callback(sslThreadId);
  CRYPTO_set_locking_callback(sslLock);
}
#endif

static void sslInfoCallback(const SSL* ssl, int where, int ret) {
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

static int createSslContext(IOArgs* args, int isFirst) {
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

  if (args->sslCipher) {
    int res = SSL_CTX_set_cipher_list(args->sslCtx, args->sslCipher);
    if (res != 1) {
      fprintf(stderr, "Set Cipher list to %s failed\n", args->sslCipher);
      return -1;
    }
  }
  return 0;
}
#endif

static int readFile(const char* name, IOThread* t) {
  int fd = open(name, O_RDONLY);
  if (fd < 0) {
    perror("Can't open input file");
    return -1;
  }

  struct stat s;
  int err = fstat(fd, &s);
  if (err != 0) {
    perror("Can't get file size");
    close(fd);
    return -2;
  }

  t->sendDataLen = s.st_size;
  t->sendData = (char*)malloc(s.st_size);
  ssize_t rc = read(fd, t->sendData, s.st_size);
  if (rc != s.st_size) {
    perror("Unable to read input file");
    close(fd);
    return -3;
  }
  close(fd);
  return 0;
}

static void waitAndReport(int duration, int warmup) {
  int durationLeft = duration;
  int toSleep;

  while (durationLeft > 0) {
    if (durationLeft < REPORT_SLEEP_TIME) {
      toSleep = durationLeft;
    } else {
      toSleep = REPORT_SLEEP_TIME;
    }

    sleep(toSleep);
    ReportInterval(stdout, duration, warmup);
    durationLeft -= toSleep;
  }
}

static void processOAuth(char* arg) {
  char* last;

  OAuthCK = strtok_r(arg, ":", &last);
  OAuthCS = strtok_r(NULL, ":", &last);
  OAuthAT = strtok_r(NULL, ":", &last);
  OAuthAS = strtok_r(NULL, ":", &last);
}

static void addHeader(char* val) {
  if (Headers == NULL) {
    Headers = (char**)malloc(sizeof(char*));
    HeadersSize = 1;
  } else if (NumHeaders == HeadersSize) {
    HeadersSize *= 2;
    Headers = (char**)realloc(Headers, sizeof(char*) * HeadersSize);
  }
  Headers[NumHeaders] = strdup(val);
  NumHeaders++;

  char* tokLast;
  char* tok;
  tok = strtok_r(val, ":", &tokLast);
  if ((tok != NULL) && !strcmp("Host", tok)) {
    HostHeaderOverride = 1;
  }
}

static void processBasic(const char* arg) {
  assert(0);
  /* TODO: Need a base64 implementation
  int inLen = strlen(arg);
  int outLen = apr_base64_encode_len(inLen);
  char* out = apr_palloc(MainPool, outLen);
  char* hdr;

  apr_base64_encode(out, arg, inLen);
  hdr = apr_psprintf(MainPool, "Authorization: Basic %s", out);
  addHeader(hdr);
  */
}

int main(int argc, char* const* argv) {
  /* Arguments */
  int duration = DEFAULT_DURATION;
  int warmupTime = DEFAULT_WARMUP;
  int doHelp = 0;
  int verbose = 0;
  char* verb = NULL;
  char* fileName = NULL;
  char* contentType = NULL;
  char* sslCipher = NULL;
  const char* url = NULL;
  char* monitorHost = NULL;
  char* monitor2Host = NULL;
  unsigned int thinkTime = 0;

  /* Globals */
  NumConnections = DEFAULT_NUM_CONNECTIONS;
  NumThreads = -1;
  ShortOutput = 0;
  RunName = "";
  KeepAlive = KEEP_ALIVE_ALWAYS;

  IOThread* threads = NULL;
  int failed = 0;
  int arg;
  do {
    arg = getopt_long(argc, argv, OPTIONS, Options, NULL);
    switch (arg) {
      case 'c':
        NumConnections = atoi(optarg);
        break;
      case 'd':
        duration = atoi(optarg);
        break;
      case 'f':
        fileName = optarg;
        break;
      case 'h':
        doHelp = 1;
        break;
      case 'k':
        KeepAlive = atoi(optarg);
        break;
      case 't':
        contentType = optarg;
        break;
      case 'u':
        processBasic(optarg);
        break;
      case 'v':
        verbose = 1;
        break;
      case 'w':
        warmupTime = atoi(optarg);
        break;
      case 'x':
        verb = optarg;
        break;
      case 'C':
        sslCipher = optarg;
        break;
      case 'H':
        addHeader(optarg);
        break;
      case 'K':
        NumThreads = atoi(optarg);
        break;
      case 'M':
        monitorHost = optarg;
        break;
      case 'X':
        monitor2Host = optarg;
        break;
      case 'N':
        RunName = optarg;
        break;
      case 'O':
        processOAuth(optarg);
        break;
      case 'S':
        ShortOutput = 1;
        break;
      case 'T':
        PrintReportingHeader(stdout);
        return 0;
        break;
      case 'W':
        thinkTime = atoi(optarg);
        break;
      case '1':
        JustOnce = 1;
        break;
      case '?':
      case ':':
        // Unknown. Error was printed.
        failed = 1;
        break;
      case -1:
        // Done!
        break;
      default:
        assert(0);
        break;
    }
  } while (arg >= 0);

  if (!failed && (optind == (argc - 1))) {
    url = argv[optind];
  } else {
    // No URL
    failed = 1;
  }

  if (failed) {
    printUsage();
    return 1;
  } else if (doHelp) {
    printUsage();
    return 0;
  }

  if (url != NULL) {
    if (url[0] == '@') {
      if (url_InitFile(url + 1) != 0) {
        fprintf(stderr, "Invalid URL file\n");
        goto finished;
      }
    } else {
      if (url_InitOne(url) != 0) {
        goto finished;
      }
    }

    if (setProcessLimits(NumConnections) != 0) {
      goto finished;
    }

    if (NumThreads < 1) {
      NumThreads = cpu_Count();
    }
    if (NumThreads > NumConnections) {
      NumThreads = NumConnections;
    }

    Running = 1;

    threads = (IOThread*)malloc(sizeof(IOThread) * NumThreads);

    RecordInit(monitorHost, monitor2Host);

    if (!JustOnce && (warmupTime > 0)) {
      RecordStart(0);
    } else {
      RecordStart(1);
    }

    for (int i = 0; i < NumThreads; i++) {
      IOThread* t = &(threads[i]);
      int numConn = NumConnections / NumThreads;
      if (i < (NumConnections % NumThreads)) {
        numConn++;
      }

      if (fileName != NULL) {
        if (readFile(fileName, t) != 0) {
          return 3;
        }
      } else {
        t->sendData = NULL;
        t->sendDataLen = 0;
      }

      if (verb == NULL) {
        if (fileName == NULL) {
          t->httpVerb = "GET";
        } else {
          t->httpVerb = "POST";
        }
      } else {
        t->httpVerb = verb;
      }

      t->index = i;
      t->keepRunning = JustOnce;
      t->numConnections = numConn;
      t->verbose = verbose;
      t->sslCipher = sslCipher;
      t->headers = Headers;
      t->numHeaders = NumHeaders;
      t->hostHeaderOverride = HostHeaderOverride;
      t->thinkTime = thinkTime;

      /*
      TODO write content-type header!
      */

      /* TODO SSL
      if (createSslContext(&(ioArgs[i]), (i == 0)) != 0) {
        goto finished;
      }
      */

      iothread_Start(t);
    }

    if (!JustOnce && (warmupTime > 0)) {
      waitAndReport(warmupTime, 1);
    }

    if (!JustOnce) {
      waitAndReport(duration, 0);
    }
    RecordStop();

    Running = 0;

    /* Sometimes threads don't terminate. Sleep for two seconds,
       then if a thread is stuck it won't affect the results much. */
    /*apr_sleep(apr_time_from_sec(2));  */
    for (int i = 0; i < NumThreads; i++) {
      iothread_Stop(&(threads[i]));
    }

  } else {
    printUsage();
    return 0;
  }

  PrintFullResults(stdout);
  EndReporting();

finished:
  return 0;
}
