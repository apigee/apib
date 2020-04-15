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

#include "test/test_server.h"

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <openssl/err.h>
#include <openssl/ssl.h>
#include <regex.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include <cassert>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <sstream>

#include "absl/strings/numbers.h"
#include "absl/strings/str_split.h"
#include "apib/addresses.h"
#include "apib/apib_lines.h"
#include "apib/apib_util.h"

using std::cerr;
using std::cout;
using std::endl;

namespace apib {

#define BACKLOG 32
#define READ_BUF 1024

static http_parser_settings ParserSettings;

static regex_t sizeParameter;
#define SIZE_PARAMETER_REGEX "^size=([0-9]+)"

TestConnection::TestConnection(TestServer* s, int fd) : server_(s), fd_(fd) {}

static std::string makeData(const int len) {
  std::string b(len, '\0');
  for (int i = 0; i < len; i++) {
    b[i] = '0' + (i % 10);
  }
  return b;
}

static void printSslError(const absl::string_view msg) {
  char buf[256];
  ERR_error_string_n(ERR_get_error(), buf, 256);
  cerr << msg << ": " << buf << endl;
}

void TestServer::success(int op) {
  assert(op < NUM_OPS);
  stats_.successes[op]++;
  stats_.successCount++;
}

void TestServer::failure() { stats_.errorCount++; }

void TestServer::socketError() { stats_.socketErrorCount++; }

void TestServer::newConnection() { stats_.connectionCount++; }

int TestConnection::write(const absl::string_view s) {
  if (ssl_ == NULL) {
    return ::write(fd_, s.data(), s.size());
  }
  return SSL_write(ssl_, s.data(), s.size());
}

void TestConnection::sendText(int code, const absl::string_view codestr,
                              const absl::string_view msg) {
  std::ostringstream out;
  out << "HTTP/1.1 " << code << ' ' << codestr << "\r\n"
      << "Server: apib test server\r\n"
      << "Content-Type: text/plain\r\n"
      << "Content-Length: " << msg.size() << "\r\n"
      << "\r\n"
      << msg;
  write(out.str());
}

void TestConnection::sendData(const absl::string_view msg) {
  std::ostringstream out;
  out << "HTTP/1.1 200 OK\r\n"
      << "Server: apib test server\r\n"
      << "Content-Type: text/plain\r\n"
      << "Content-Length: " << msg.size() << "\r\n"
      << "\r\n"
      << msg;
  write(out.str());
}

// Called by http_parser to collect the request body
static int parsedBody(http_parser* p, const char* buf, size_t len) {
  auto c = reinterpret_cast<TestConnection*>(p->data);
  c->setBody(std::string(buf, len));
  return 0;
}

void TestConnection::setBody(const absl::string_view bs) {
  body_ = std::string(bs);
}

// Called by http_parser when we get the URL.
static int parsedUrl(http_parser* p, const char* buf, size_t len) {
  auto c = reinterpret_cast<TestConnection*>(p->data);
  c->setQuery(std::string(buf, len));
  return 0;
}

void TestConnection::setQuery(const absl::string_view qs) {
  const std::vector<std::string> pathQuery = absl::StrSplit(qs, '?');
  if (pathQuery.empty()) {
    // Nothing to parse here
    return;
  }

  path_ = pathQuery[0];

  if (pathQuery.size() == 1) {
    // No query
    return;
  }

  const std::vector<std::string> qps = absl::StrSplit(pathQuery[1], '&');
  for (auto it = qps.cbegin(); it != qps.cend(); it++) {
    // TODO maybe only split(1)?
    const std::vector<std::string> nv = absl::StrSplit(*it, '=');
    const std::string name = nv[0];
    std::string value;
    if (nv.size() > 1) {
      value = nv[1];
    }
    query_[name] = value;
  }
}

static int parseComplete(http_parser* p) {
  auto c = static_cast<TestConnection*>(p->data);
  c->setParseComplete();
  return 0;
}

static int parsedHeaderField(http_parser* p, const char* buf, size_t len) {
  auto c = static_cast<TestConnection*>(p->data);
  c->setNextHeaderName(absl::string_view(buf, len));
  return 0;
}

static int parsedHeaderValue(http_parser* p, const char* buf, size_t len) {
  auto c = static_cast<TestConnection*>(p->data);
  c->setHeaderValue(absl::string_view(buf, len));
  return 0;
}

void TestConnection::setHeaderValue(const absl::string_view v) {
  if (eqcase("Connection", nextHeader_) && eqcase("close", v)) {
    shouldClose_ = true;
  } else if (eqcase("Authorization", nextHeader_) &&
             ("Basic dGVzdDp2ZXJ5dmVyeXNlY3JldA==" != v)) {
    // Checked for the authorization "test:veryverysecret"
    notAuthorized_ = false;
  } else if (eqcase("X-Sleep", nextHeader_)) {
    int tmpSleep;
    if (absl::SimpleAtoi(v, &tmpSleep)) {
      sleepTime_ = tmpSleep;
    }
  }
}

void TestConnection::handleRequest() {
  if (notAuthorized_) {
    sendText(401, "Not authorized", "Wrong password!\n");
    return;
  }

  if (sleepTime_ > 0) {
    sleep(sleepTime_);
  }

  if ("/hello" == path_) {
    if (parser_.method == HTTP_GET) {
      sendText(200, "OK", "Hello, World!\n");
      server_->success(OP_HELLO);
    } else {
      sendText(405, "BAD METHOD", "Wrong method");
      server_->failure();
    }

  } else if ("/data" == path_) {
    if (parser_.method == HTTP_GET) {
      int size = 1024;
      if (!query_["size"].empty()) {
        size = stoi(query_["size"]);
      }
      sendData(makeData(size));
      server_->success(OP_DATA);
    } else {
      sendText(405, "BAD METHOD", "Wrong method");
      server_->failure();
    }

  } else if ("/echo" == path_) {
    if (parser_.method == HTTP_POST) {
      sendData(body());
      server_->success(OP_ECHO);
    } else {
      sendText(405, "BAD METHOD", "Wrong method");
      server_->failure();
    }
  } else {
    sendText(404, "NOT FOUND", "Not found");
    server_->failure();
  }
}

ssize_t TestConnection::httpTransaction(char* buf, ssize_t bufPos) {
  done_ = false;
  http_parser_init(&parser_, HTTP_REQUEST);
  parser_.data = this;

  do {
    int readCount;
    if (ssl_ == nullptr) {
      readCount = ::read(fd_, buf + bufPos, READ_BUF - bufPos);
    } else {
      readCount = SSL_read(ssl_, buf + bufPos, READ_BUF - bufPos);
    }

    if (readCount < 0) {
      if (ssl_ == NULL) {
        perror("Error on read from socket");
      } else {
        printSslError("Error on socket read");
      }
      server_->socketError();
      return -1;
    } else if (readCount == 0) {
      return -2;
    }

    const size_t available = bufPos + readCount;
    const size_t parseCount =
        http_parser_execute(&parser_, &ParserSettings, buf, available);

    if (parser_.http_errno != 0) {
      fprintf(stderr, "Error parsing HTTP request: %i: %s\n",
              parser_.http_errno,
              http_errno_description((http_errno)parser_.http_errno));
      return -1;
    }

    if (parseCount < available) {
      const size_t leftover = available - parseCount;
      memmove(buf, buf + parseCount, leftover);
      bufPos = leftover;
    } else {
      bufPos = 0;
    }
  } while (!done_);

  handleRequest();
  if (shouldClose_) {
    return -1;
  }
  return bufPos;
}

void TestConnection::socketLoop() {
  ssize_t bufPos = 0;
  char* buf = (char*)malloc(READ_BUF);

  if (server_->ssl() != nullptr) {
    ssl_ = SSL_new(server_->ssl());
    int err = SSL_set_fd(ssl_, fd_);
    if (err != 1) {
      printSslError("Can't connect to SSL FD");
      goto finish;
    }
    SSL_set_accept_state(ssl_);
  }

  server_->newConnection();

  do {
    bufPos = httpTransaction(buf, bufPos);
    path_.clear();
    query_.clear();
    body_.clear();
    shouldClose_ = false;
    notAuthorized_ = false;
    sleepTime_ = 0;
  } while (bufPos >= 0);

finish:
  close(fd_);
  free(buf);
  if (ssl_ != nullptr) {
    SSL_free(ssl_);
  }
  delete this;
}

void TestServer::acceptLoop() { ev_run(loop_, 0); }

void TestServer::acceptOne() {
  const int fd = accept(listenfd_, nullptr, nullptr);
  if (fd < 0) {
    // This could be because the socket was closed.
    perror("Error accepting socket");
    return;
  }

  // Explicitly clear the blocking flag, because it might be inherited from
  // the accept FD.
  int flags = fcntl(fd, F_GETFL);
  flags &= ~O_NONBLOCK;
  int err = fcntl(fd, F_SETFL, flags);
  mandatoryAssert(err == 0);

  TestConnection* c = new TestConnection(this, fd);
  std::thread ct(std::bind(&TestConnection::socketLoop, c));
  ct.detach();
}

int TestServer::initializeSsl(const std::string& keyFile,
                              const std::string& certFile) {
  sslCtx_ = SSL_CTX_new(TLS_server_method());

  int err = SSL_CTX_use_certificate_chain_file(sslCtx_, certFile.c_str());
  if (err != 1) {
    printSslError("Can't load certificate file");
    return -1;
  }

  err = SSL_CTX_use_PrivateKey_file(sslCtx_, keyFile.c_str(), SSL_FILETYPE_PEM);
  if (err != 1) {
    printSslError("Can't load key file");
    return -2;
  }

  return 0;
}

static void handleAccept(struct ev_loop* loop, ev_io* io, int e) {
  auto s = reinterpret_cast<TestServer*>(io->data);
  s->acceptOne();
}

static void handleShutdown(struct ev_loop* loop, ev_async* a, int e) {
  // Unreference the listener fd and this handler and then the loop will exit!
  ev_unref(loop);
  ev_unref(loop);
}

int TestServer::start(const std::string& address, int port,
                      const std::string& keyFile, const std::string& certFile) {
  int err = regcomp(&sizeParameter, SIZE_PARAMETER_REGEX, REG_EXTENDED);
  assert(err == 0);

  http_parser_settings_init(&ParserSettings);
  ParserSettings.on_url = parsedUrl;
  ParserSettings.on_header_field = parsedHeaderField;
  ParserSettings.on_header_value = parsedHeaderValue;
  ParserSettings.on_body = parsedBody;
  ParserSettings.on_message_complete = parseComplete;

  if (!keyFile.empty() && !certFile.empty()) {
    if (initializeSsl(keyFile, certFile) != 0) {
      return -1;
    }
  }

  auto as = Addresses::lookup(address);
  if (!as.ok()) {
    cerr << "Invalid listen address: " << as << '\n';
    return -1;
  }

  const Address listenAddr = as.valueref()->get(port);

  loop_ = ev_loop_new(EVFLAG_AUTO);

  listenfd_ = socket(listenAddr.family(), SOCK_STREAM, 0);
  if (listenfd_ < 0) {
    perror("Cant' create socket");
    return -1;
  }

  err = fcntl(listenfd_, F_SETFL, O_NONBLOCK);
  if (err != 0) {
    perror("Can't set socket mode");
    return -2;
  }

  struct sockaddr_storage addr;
  const socklen_t addrlen = listenAddr.get(&addr);
  err = bind(listenfd_, (const struct sockaddr*)&addr, addrlen);
  if (err != 0) {
    perror("Can't bind to port");
    return -2;
  }

  err = listen(listenfd_, BACKLOG);
  if (err != 0) {
    perror("Can't listen on socket");
    return -3;
  }

  ev_async_init(&shutdownev_, handleShutdown);
  ev_async_start(loop_, &shutdownev_);

  ev_io_init(&listenev_, handleAccept, listenfd_, EV_READ);
  listenev_.data = this;
  ev_io_start(loop_, &listenev_);

  acceptThread_.reset(
      new std::thread(std::bind(&TestServer::acceptLoop, this)));
  return 0;
}

TestServer::~TestServer() {
  if (loop_ != nullptr) {
    ev_loop_destroy(loop_);
  }
  if (sslCtx_ != nullptr) {
    SSL_CTX_free(sslCtx_);
  }
}

int TestServer::port() const {
  struct sockaddr_storage addr;
  socklen_t addrlen = sizeof(struct sockaddr_storage);

  getsockname(listenfd_, (struct sockaddr*)&addr, &addrlen);
  const Address a((struct sockaddr*)&addr, addrlen);
  return a.port();
}

void TestServer::resetStats() { stats_.reset(); }

TestServerStats TestServer::stats() const { return stats_; }

void TestServer::stop() {
  ev_async_send(loop_, &shutdownev_);
  acceptThread_->join();
  close(listenfd_);
}

void TestServer::join() { acceptThread_->join(); }

TestServerStats::TestServerStats() {
  for (int i = 0; i < NUM_OPS; i++) {
    successes[i] = 0;
  }
}

TestServerStats::TestServerStats(const TestServerStats& s) {
  connectionCount.store(s.connectionCount);
  socketErrorCount.store(s.socketErrorCount);
  errorCount.store(s.errorCount);
  successCount.store(s.successCount);
  for (int i = 0; i < NUM_OPS; i++) {
    successes[i].store(s.successes[i]);
  }
}

void TestServerStats::reset() {
  connectionCount = 0;
  socketErrorCount = 0;
  errorCount = 0;
  successCount = 0;
  for (int i = 0; i < NUM_OPS; i++) {
    successes[i] = 0;
  }
}

}  // namespace apib