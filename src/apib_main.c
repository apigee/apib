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
#include "src/apib_oauth.h"
#include "src/apib_reporting.h"
#include "src/apib_url.h"
#include "src/apib_util.h"
#include "third_party/base64.h"

#define DEFAULT_CONTENT_TYPE "application/octet-stream"
#define KEEP_ALIVE_ALWAYS -1
#define KEEP_ALIVE_NEVER 0

/* Globals */
static int ShortOutput;
static char *RunName;
static int NumConnections;
static int NumThreads;
static int JustOnce = 0;
static int KeepAlive;
static char *Verb = NULL;
static char *FileName = NULL;
static char *ContentType = NULL;
static char *SslCipher = NULL;
static int Verbose = 0;
static int ThinkTime = 0;

static char **Headers = NULL;
static unsigned int HeadersSize = 0;
static unsigned int NumHeaders = 0;
static int HostHeaderOverride = 0;

static OAuthInfo *OAuth = NULL;

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
  "-k --keep-alive         Keep-alive duration:\n"                             \
  "      0 to disable, non-zero for timeout\n"                                 \
  "-t --content-type       Value of the Content-Type header\n"                 \
  "-u --username-password  Credentials for HTTP Basic authentication\n"        \
  "       in username:password format\n"                                       \
  "-v --verbose            Verbose output\n"                                   \
  "-w --warmup             Warm-up duration, in seconds (default 0)\n"         \
  "-x --method             HTTP request method (default GET)\n"                \
  "-C --cipherlist         Cipher list offered to server for HTTPS\n"          \
  "-H --header             HTTP header line in Name: Value format\n"           \
  "-K --iothreads          Number of I/O threads to spawn\n"                   \
  "       default == number of CPU cores\n"                                    \
  "-N --name               Name to put in CSV output to identify test run\n"   \
  "-O --oauth              OAuth 1.0 signature\n"                              \
  "       in format consumerkey:secret:token:secret\n"                         \
  "-S --csv-output         Output all test results in a single CSV line\n"     \
  "-T --header-line        Do not run, but output a single CSV header line\n"  \
  "-W --think-time         Think time to wait in between requests\n"           \
  "        in milliseconds\n"                                                  \
  "-M --monitor            Host name and port number of apibmon\n"             \
  "-X --monitor2           Second host name and port number of apibmon\n"      \
  "\n"                                                                         \
  "The last argument may be an http or https URL, or an \"@\" symbol\n"        \
  "followed by a file name. If a file name, then apib will read the file\n"    \
  "as a list of URLs, one per line, and randomly test each one.\n"             \
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
            "Current hard file descriptor limit is %lu: it is too low. Try "
            "sudo.\n",
            limits.rlim_max);
    return -1;
  }
}

static void sslInfoCallback(const SSL *ssl, int where, int ret) {
  printf("OpenSSL: %s\n", SSL_state_string_long(ssl));
  if (ret == SSL_CB_ALERT) {
    printf("  alert: %s\n", SSL_alert_desc_string_long(ret));
  } else if (ret == 0) {
    printf("  Error occurred\n");
  }
  if (where & SSL_CB_HANDSHAKE_DONE) {
    int bits;
    SSL_get_cipher_bits(ssl, &bits);
    printf("  Protocol: %s\n", SSL_get_cipher_version(ssl));
    printf("  Cipher: %s (%i bits)\n", SSL_get_cipher_name(ssl), bits);
  }
}

static int createSslContext(IOThread *t) {
  t->sslCtx = SSL_CTX_new(TLS_client_method());
  SSL_CTX_set_mode(t->sslCtx, SSL_MODE_ENABLE_PARTIAL_WRITE);
  SSL_CTX_set_default_verify_paths(t->sslCtx);
  if (t->verbose) {
    SSL_CTX_set_info_callback(t->sslCtx, sslInfoCallback);
  }

  if (t->sslCipher) {
    int res = SSL_CTX_set_cipher_list(t->sslCtx, t->sslCipher);
    if (res != 1) {
      fprintf(stderr, "Set Cipher list to %s failed\n", t->sslCipher);
      return -1;
    }
  }
  return 0;
}

static int readFile(const char *name, IOThread *t) {
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
  t->sendData = (char *)malloc(s.st_size);
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
    if (!ShortOutput) {
      ReportInterval(stdout, duration, warmup);
    }
    durationLeft -= toSleep;
  }
}

static void processOAuth(char *arg) {
  OAuth = (OAuthInfo *)malloc(sizeof(OAuthInfo));
  char *last;
  OAuth->consumerKey = strtok_r(arg, ":", &last);
  OAuth->consumerSecret = strtok_r(NULL, ":", &last);
  OAuth->accessToken = strtok_r(NULL, ":", &last);
  OAuth->tokenSecret = strtok_r(NULL, ":", &last);
}

static void addHeader(char *val) {
  if (Headers == NULL) {
    Headers = (char **)malloc(sizeof(char *));
    HeadersSize = 1;
  } else if (NumHeaders == HeadersSize) {
    HeadersSize *= 2;
    Headers = (char **)realloc(Headers, sizeof(char *) * HeadersSize);
  }
  Headers[NumHeaders] = strdup(val);
  NumHeaders++;

  char *tokLast;
  char *tok;
  tok = strtok_r(val, ":", &tokLast);
  if ((tok != NULL) && !strcmp("Host", tok)) {
    HostHeaderOverride = 1;
  }
}

static void processBasic(const char *arg) {
  const size_t inLen = strlen(arg);
  const int encLen = Base64encode_len(inLen);
  char *b64 = (char *)malloc(encLen + 1);
  Base64encode(b64, arg, inLen);

  const size_t hdrLen = encLen + 21;
  char *hdr = malloc(hdrLen);
  safeSprintf(hdr, hdrLen, "Authorization: Basic %s", b64);
  free(b64);
  addHeader(hdr);
}

static int initializeThread(int ix, IOThread* t) {
  int numConn = NumConnections / NumThreads;
  if (ix < (NumConnections % NumThreads)) {
    numConn++;
  }

  if (FileName != NULL) {
    if (readFile(FileName, t) != 0) {
      return 3;
    }
  } else {
    t->sendData = NULL;
    t->sendDataLen = 0;
  }

  if (Verb == NULL) {
    if (FileName == NULL) {
      t->httpVerb = "GET";
    } else {
      t->httpVerb = "POST";
    }
  } else {
    t->httpVerb = Verb;
  }

  t->index = ix;
  t->keepRunning = (JustOnce ? -1 : 1);
  t->numConnections = numConn;
  t->verbose = Verbose;
  t->sslCipher = SslCipher;
  t->headers = Headers;
  t->numHeaders = NumHeaders;
  t->hostHeaderOverride = HostHeaderOverride;
  t->thinkTime = ThinkTime;
  t->noKeepAlive = (KeepAlive != KEEP_ALIVE_ALWAYS);
  t->oauth = OAuth;

  return createSslContext(t);
}

int main(int argc, char *const *argv) {
  /* Arguments */
  int duration = DEFAULT_DURATION;
  int warmupTime = DEFAULT_WARMUP;
  int doHelp = 0;
  const char *url = NULL;
  char *monitorHost = NULL;
  char *monitor2Host = NULL;

  /* Globals */
  NumConnections = DEFAULT_NUM_CONNECTIONS;
  NumThreads = -1;
  ShortOutput = 0;
  RunName = "";
  KeepAlive = KEEP_ALIVE_ALWAYS;

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
        FileName = optarg;
        break;
      case 'h':
        doHelp = 1;
        break;
      case 'k':
        KeepAlive = atoi(optarg);
        break;
      case 't':
        ContentType = optarg;
        break;
      case 'u':
        processBasic(optarg);
        break;
      case 'v':
        Verbose = 1;
        break;
      case 'w':
        warmupTime = atoi(optarg);
        break;
      case 'x':
        Verb = optarg;
        break;
      case 'C':
        SslCipher = optarg;
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
        ThinkTime = atoi(optarg);
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

  if (ContentType != NULL) {
    char buf[256];
    safeSprintf(buf, 256, "Content-Type: %s", ContentType);
    addHeader(buf);
  }

  if (url != NULL) {
    if (url[0] == '@') {
      if (url_InitFile(url + 1) != 0) {
        fprintf(stderr, "Invalid URL file\n");
        goto finished;
      }
    } else {
      if (url_InitOne(url) != 0) {
        fprintf(stderr, "Invalid url: \"%s\"\n", url);
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

    RecordInit(monitorHost, monitor2Host);

    if (JustOnce) {
      IOThread thread;
      int err = initializeThread(0, &thread);
      if (err != 0) {
	goto finished;
      }
      RecordStart(1);
      iothread_Start(&thread);
      iothread_Join(&thread);
      RecordStop();

    } else {
      IOThread* threads = (IOThread *)malloc(sizeof(IOThread) * NumThreads);
      for (int i = 0; i < NumThreads; i++) {
	int err = initializeThread(i, &(threads[i]));
	if (err != 0) {
	  goto finished;
	}
	iothread_Start(&(threads[i]));
      }

      if (warmupTime > 0) {
	RecordStart(1);
	waitAndReport(warmupTime, 1);
      }
      RecordStart(1);
      waitAndReport(duration, 0);
      RecordStop();

      for (int i = 0; (i < NumThreads); i++) {
	iothread_RequestStop(&(threads[i]), 2);
      }
      for (int i = 0; i < NumThreads; i++) {
	iothread_Join(&(threads[i]));
      }
      free(threads);
    }
  } else {
    printUsage();
    return 0;
  }

  if (ShortOutput) {
    PrintShortResults(stdout, RunName, NumThreads, NumConnections);
  } else {
    PrintFullResults(stdout);
  }
  EndReporting();

finished:
  return 0;
}
