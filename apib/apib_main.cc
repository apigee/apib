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

#include <getopt.h>
#include <sys/resource.h>
#include <unistd.h>

#include <cstring>
#include <ctime>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "absl/strings/numbers.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_split.h"
#include "apib/apib_cpu.h"
#include "apib/apib_iothread.h"
#include "apib/apib_oauth.h"
#include "apib/apib_reporting.h"
#include "apib/apib_url.h"
#include "apib/apib_util.h"
#include "third_party/base64.h"

static const std::string kApibVersion = "1.2";

using apib::eqcase;
using apib::IOThread;
using apib::OAuthInfo;
using apib::RecordInit;
using apib::RecordStart;
using apib::RecordStop;
using apib::ReportInterval;
using apib::URLInfo;
using std::cerr;
using std::cout;
using std::endl;

static const int KeepAliveAlways = -1;
static const int DefaultNumConnections = 1;
static const int DefaultDuration = 60;
static const int DefaultWarmup = 0;
static const int ReportSleepTime = 5;

static int ShortOutput = 0;
static std::string RunName;
static int NumConnections = DefaultNumConnections;
static int NumThreads = -1;
static bool JustOnce = false;
static int KeepAlive = KeepAliveAlways;
static std::string Verb;
static std::string FileName;
static std::string ContentType;
static std::string SslCipher;
static bool SslVerify = false;
static std::string SslCertificate;
static bool Verbose = false;
static int ThinkTime = 0;
static std::vector<std::string> Headers;
static int SetHeaders = 0;

static OAuthInfo *OAuth = nullptr;

static const char *const OPTIONS = "c:d:f:hk:t:u:vw:x:C:F:H:O:K:M:X:N:STVW:Z1";

static const struct option Options[] = {
    {"concurrency", required_argument, NULL, 'c'},
    {"duration", required_argument, NULL, 'd'},
    {"input-file", required_argument, NULL, 'f'},
    {"help", no_argument, NULL, 'h'},
    {"keep-alive", required_argument, NULL, 'k'},
    {"content-type", required_argument, NULL, 't'},
    {"username-password", required_argument, NULL, 'u'},
    {"verbose", no_argument, NULL, 'v'},
    {"version", no_argument, NULL, 'Z'},
    {"warmup", required_argument, NULL, 'w'},
    {"method", required_argument, NULL, 'x'},
    {"cipherlist", required_argument, NULL, 'C'},
    {"certificate", required_argument, NULL, 'F'},
    {"header", required_argument, NULL, 'H'},
    {"oauth", required_argument, NULL, 'O'},
    {"iothreads", required_argument, NULL, 'K'},
    {"monitor", required_argument, NULL, 'M'},
    {"monitor2", required_argument, NULL, 'X'},
    {"name", required_argument, NULL, 'N'},
    {"csv-output", no_argument, NULL, 'S'},
    {"header-line", no_argument, NULL, 'T'},
    {"verify", no_argument, NULL, 'V'},
    {"one", no_argument, NULL, '1'},
    {"think-time", required_argument, NULL, 'W'}};

static const char *const USAGE_DOCS =
    "-1 --one                Send just one request and exit\n"
    "-c --concurrency        Number of concurrent requests (default 1)\n"
    "-d --duration           Test duration in seconds\n"
    "-f --input-file         File name to send on PUT and POST requests\n"
    "-h --help               Display this message\n"
    "-k --keep-alive         Keep-alive duration:\n"
    "      0 to disable, non-zero for timeout\n"
    "-t --content-type       Value of the Content-Type header\n"
    "-u --username-password  Credentials for HTTP Basic authentication\n"
    "       in username:password format\n"
    "-v --verbose            Verbose output\n"
    "   --version            Version information\n"
    "-w --warmup             Warm-up duration, in seconds (default 0)\n"
    "-x --method             HTTP request method (default GET)\n"
    "-C --cipherlist         Cipher list offered to server for HTTPS\n"
    "-F --certificate        PEM file containing CA certificates to trust\n"
    "-H --header             HTTP header line in Name: Value format\n"
    "-K --iothreads          Number of I/O threads to spawn\n"
    "       default == number of CPU cores\n"
    "-N --name               Name to put in CSV output to identify test run\n"
    "-O --oauth              OAuth 1.0 signature\n"
    "       in format consumerkey:secret:token:secret\n"
    "-S --csv-output         Output all test results in a single CSV line\n"
    "-T --header-line        Do not run, but output a single CSV header line\n"
    "-V --verify             Verify TLS peer\n"
    "-W --think-time         Think time to wait in between requests\n"
    "        in milliseconds\n"
    "-M --monitor            Host name and port number of apibmon\n"
    "-X --monitor2           Second host name and port number of apibmon\n"
    "\n"
    "The last argument may be an http or https URL, or an \"@\" symbol\n"
    "followed by a file name. If a file name, then apib will read the file\n"
    "as a list of URLs, one per line, and randomly test each one.\n"
    "\n"
    "  if -S is used then output is CSV-separated on one line:\n"
    "  name,throughput,avg. "
    "latency,threads,connections,duration,completed,successful,errors,sockets,"
    "min. latency,max. latency,50%,90%,98%,99%\n"
    "\n"
    "  if -O is used then the value is four parameters, separated by a colon:\n"
    "  consumer key:secret:token:secret. You may omit the last two.\n";

static void printUsage() {
  cerr << "Usage: apib [options] [URL | @file]" << endl;
  cerr << USAGE_DOCS << endl;
}

static void printLibraryInfo() {
  cout << "apib " << kApibVersion << '\n';
  cout << "  (git commit $Id$)\n";
  cout << "libev " << ev_version_major() << '.' << ev_version_minor() << '\n';
  cout << "  Supported backends: "
       << IOThread::GetEvBackends(ev_supported_backends()) << '\n';
  cout << "  Recommended backends: "
       << IOThread::GetEvBackends(ev_recommended_backends()) << '\n';
  cout << "http_parser " << HTTP_PARSER_VERSION_MAJOR << '.'
       << HTTP_PARSER_VERSION_MINOR << '.' << HTTP_PARSER_VERSION_PATCH << '\n';
  cout << OpenSSL_version(OPENSSL_VERSION) << '\n';
}

static int setProcessLimits(int numConnections) {
  struct rlimit limits;
  int err;

  getrlimit(RLIMIT_NOFILE, &limits);
  if (numConnections < (int)limits.rlim_cur) {
    return 0;
  }
  if (numConnections < (int)limits.rlim_max) {
    limits.rlim_cur = limits.rlim_max;
    err = setrlimit(RLIMIT_NOFILE, &limits);
    if (err == 0) {
      return 0;
    } else {
      cerr << "Error setting file descriptor limit: " << err << endl;
      return -1;
    }
  } else {
    cerr << "Current hard file descriptor limit is " << limits.rlim_max
         << ": it is too low. Try sudo" << endl;
    return -1;
  }
}

static void sslInfoCallback(const SSL *ssl, int where, int ret) {
  cout << "OpenSSL: " << SSL_state_string_long(ssl) << endl;
  if (ret == SSL_CB_ALERT) {
    cout << "  alert: " << SSL_alert_desc_string_long(ret) << endl;
  } else if (ret == 0) {
    cout << "  Error occurred" << endl;
  }
  if (where & SSL_CB_HANDSHAKE_DONE) {
    int bits;
    SSL_get_cipher_bits(ssl, &bits);
    cout << "  Protocol: " << SSL_get_cipher_version(ssl) << endl;
    cout << "  Cipher: " << SSL_get_cipher_name(ssl) << " (" << bits << " bits)"
         << endl;
  }
}

static int createSslContext(IOThread *t) {
  t->sslCtx = SSL_CTX_new(TLS_client_method());
  SSL_CTX_set_mode(t->sslCtx, SSL_MODE_ENABLE_PARTIAL_WRITE);
  SSL_CTX_set_default_verify_paths(t->sslCtx);
  if (SslVerify) {
    SSL_CTX_set_verify(t->sslCtx, SSL_VERIFY_PEER, NULL);
  }
  if (!SslCertificate.empty()) {
    int err =
        SSL_CTX_load_verify_locations(t->sslCtx, SslCertificate.c_str(), NULL);
    if (err != 1) {
      cerr << "Could not load CA certificates from " << SslCertificate << endl;
      return -2;
    }
  }
  if (t->verbose) {
    SSL_CTX_set_info_callback(t->sslCtx, sslInfoCallback);
  }

  if (!t->sslCipher.empty()) {
    int res = SSL_CTX_set_cipher_list(t->sslCtx, t->sslCipher.c_str());
    if (res != 1) {
      cerr << "Set Cipher list to " << t->sslCipher << " failed" << endl;
      return -1;
    }
  }
  return 0;
}

static int readFile(const std::string &name, IOThread *t) {
  // Open and seek to the end
  std::ifstream in(name, std::ios::binary | std::ios::ate);
  if (!in) {
    cerr << "Cannot open input file " << name << endl;
    return -1;
  }

  const auto size = in.tellg();
  std::string buf(size, '\0');
  in.seekg(0);
  in.read(&buf[0], size);
  t->sendData = std::move(buf);
  return 0;
}

static void waitAndReport(const apib::ThreadList &threads, int duration,
                          bool warmup) {
  int durationLeft = duration;
  int toSleep;

  while (durationLeft > 0) {
    if (durationLeft < ReportSleepTime) {
      toSleep = durationLeft;
    } else {
      toSleep = ReportSleepTime;
    }

    sleep(toSleep);
    if (ShortOutput) {
      apib::SampleCPU();
    } else {
      ReportInterval(std::cout, threads, duration, warmup);
    }
    durationLeft -= toSleep;
  }
}

static void processOAuth(const absl::string_view arg) {
  const std::vector<std::string> parts = absl::StrSplit(arg, ':');
  OAuth = new OAuthInfo();
  if (parts.size() > 0) {
    OAuth->consumerKey = parts[0];
  }
  if (parts.size() > 1) {
    OAuth->consumerSecret = parts[1];
  }
  if (parts.size() > 2) {
    OAuth->accessToken = parts[2];
  }
  if (parts.size() > 3) {
    OAuth->tokenSecret = parts[3];
  }
  SetHeaders |= IOThread::kAuthorizationSet;
}

static void addHeader(const absl::string_view val) {
  const std::vector<std::string> parts = absl::StrSplit(val, ':');
  if (parts.empty()) {
    cerr << "Invalid header: " << val << endl;
    return;
  }

  const std::string name = parts[0];
  if (eqcase(name, "Host")) {
    SetHeaders |= IOThread::kHostSet;
  } else if (eqcase(name, "Content-Length")) {
    SetHeaders |= IOThread::kContentLengthSet;
  } else if (eqcase(name, "Content-Type")) {
    SetHeaders |= IOThread::kContentTypeSet;
  } else if (eqcase(name, "Authorization")) {
    SetHeaders |= IOThread::kAuthorizationSet;
  } else if (eqcase(name, "Connection")) {
    SetHeaders |= IOThread::kConnectionSet;
  } else if (eqcase(name, "User-Agent")) {
    SetHeaders |= IOThread::kUserAgentSet;
  }

  Headers.push_back(std::string(val));
}

static void processBasic(const std::string &arg) {
  // TODO more C++y
  const int encLen = Base64encode_len(arg.size());
  char *b64 = new char[encLen + 1];
  Base64encode(b64, arg.c_str(), arg.size());

  addHeader(absl::StrCat("Authorization: Basic ", b64));
}

static int initializeThread(int ix, IOThread *t) {
  int numConn = NumConnections / NumThreads;
  if (ix < (NumConnections % NumThreads)) {
    numConn++;
  }

  if (!FileName.empty()) {
    if (readFile(FileName, t) != 0) {
      return 3;
    }
  }

  if (Verb.empty()) {
    if (FileName.empty()) {
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
  t->headers = &Headers;
  t->headersSet = SetHeaders;
  t->thinkTime = ThinkTime;
  t->noKeepAlive = (KeepAlive != KeepAliveAlways);
  t->oauth = OAuth;

  return createSslContext(t);
}

int main(int argc, char *const *argv) {
  /* Arguments */
  int duration = DefaultDuration;
  int warmupTime = DefaultWarmup;
  bool doHelp = false;
  bool doVersion = false;
  std::string url;
  std::string monitorHost;
  std::string monitor2Host;

  bool failed = false;
  int arg;
  do {
    arg = getopt_long(argc, argv, OPTIONS, Options, NULL);
    switch (arg) {
      case 'c':
        if (!absl::SimpleAtoi(optarg, &NumConnections)) {
          failed = true;
        }
        break;
      case 'd':
        if (!absl::SimpleAtoi(optarg, &duration)) {
          failed = true;
        }
        break;
      case 'f':
        FileName = optarg;
        break;
      case 'h':
        doHelp = true;
        break;
      case 'k':
        if (!absl::SimpleAtoi(optarg, &KeepAlive)) {
          failed = true;
        }
        break;
      case 't':
        ContentType = optarg;
        break;
      case 'u':
        processBasic(optarg);
        break;
      case 'v':
        Verbose = true;
        break;
      case 'w':
        if (!absl::SimpleAtoi(optarg, &warmupTime)) {
          failed = true;
        }
        break;
      case 'x':
        Verb = optarg;
        break;
      case 'Z':
        doVersion = true;
        break;
      case 'C':
        SslCipher = optarg;
        break;
      case 'F':
        SslCertificate = optarg;
        break;
      case 'H':
        addHeader(optarg);
        break;
      case 'K':
        if (!absl::SimpleAtoi(optarg, &NumThreads)) {
          failed = true;
        }
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
        ShortOutput = true;
        break;
      case 'T':
        apib::PrintReportingHeader(std::cout);
        return 0;
        break;
      case 'V':
        SslVerify = true;
        break;
      case 'W':
        if (!absl::SimpleAtoi(optarg, &ThinkTime)) {
          failed = true;
        }
        break;
      case '1':
        JustOnce = true;
        break;
      case '?':
      case ':':
        // Unknown. Error was printed.
        failed = true;
        break;
      case -1:
        // Done!
        break;
      default:
        cerr << "Internal error: Unknown option " << arg << endl;
        std::abort();
        break;
    }
  } while (arg >= 0);

  if (doHelp) {
    printUsage();
    return 0;
  } else if (doVersion) {
    printLibraryInfo();
    return 0;
  }

  if (!failed && (optind == (argc - 1))) {
    url = argv[optind];
  } else {
    // No URL
    failed = 1;
  }

  if (failed) {
    printUsage();
    return 1;
  }

  if (!ContentType.empty()) {
    std::ostringstream hdr;
    hdr << "Content-Type: " << ContentType;
    addHeader(hdr.str());
  }

  if (!url.empty()) {
    if (url[0] == '@') {
      const auto s = URLInfo::InitFile(url.substr(1));
      if (!s.ok()) {
        cerr << "Error opening URL file: " << s << '\n';
        goto finished;
      }
    } else {
      const auto s = URLInfo::InitOne(url);
      if (!s.ok()) {
        cerr << s << '\n';
        goto finished;
      }
    }

    if (setProcessLimits(NumConnections) != 0) {
      goto finished;
    }

    if (NumThreads < 1) {
      NumThreads = apib::cpu_Count();
    }
    if (NumThreads > NumConnections) {
      NumThreads = NumConnections;
    }

    if (Verbose) {
      printLibraryInfo();
    }

    RecordInit(monitorHost, monitor2Host);

    apib::ThreadList threads;
    if (JustOnce) {
      threads.push_back(std::unique_ptr<IOThread>(new IOThread()));
      int err = initializeThread(0, threads[0].get());
      if (err != 0) {
        goto finished;
      }
      RecordStart(true, threads);
      threads[0]->Start();
      threads[0]->Join();
      RecordStop(threads);

    } else {
      for (int i = 0; i < NumThreads; i++) {
        threads.push_back(std::unique_ptr<IOThread>(new IOThread()));
        int err = initializeThread(i, threads[i].get());
        if (err != 0) {
          goto finished;
        }
        threads[i]->Start();
      }

      if (warmupTime > 0) {
        RecordStart(true, threads);
        waitAndReport(threads, warmupTime, true);
      }
      RecordStart(true, threads);
      waitAndReport(threads, duration, false);
      RecordStop(threads);

      for (auto it = threads.begin(); it != threads.end(); it++) {
        (*it)->RequestStop(2);
      }
      for (auto it = threads.begin(); it != threads.end(); it++) {
        (*it)->Join();
      }
    }
  } else {
    printUsage();
    return 0;
  }

  if (ShortOutput) {
    apib::PrintShortResults(std::cout, RunName, NumThreads, NumConnections);
  } else {
    apib::PrintFullResults(std::cout);
  }
  apib::EndReporting();

finished:
  return 0;
}
