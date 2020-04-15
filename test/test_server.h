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

#ifndef APIB_TEST_SERVER_H
#define APIB_TEST_SERVER_H

#include <openssl/ssl.h>

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "absl/strings/string_view.h"
#include "ev.h"
#include "third_party/http_parser/http_parser.h"

namespace apib {

#define OP_HELLO 0
#define OP_ECHO 1
#define OP_DATA 2
#define NUM_OPS 3

class TestServerStats {
 public:
  TestServerStats();
  TestServerStats(const TestServerStats& s);
  void reset();

  std::atomic_int32_t connectionCount;
  std::atomic_int32_t socketErrorCount;
  std::atomic_int32_t errorCount;
  std::atomic_int32_t successCount;
  std::atomic_int32_t successes[NUM_OPS];
};

class TestServer {
 public:
  ~TestServer();
  /* Start a simple and dumb thread-per-socket HTTP server on a thread. */
  int start(const std::string& address, int port, const std::string& keyFile,
            const std::string& certFile);
  int port() const;
  TestServerStats stats() const;
  void resetStats();
  void stop();
  void join();
  void success(int op);
  void failure();
  void socketError();
  void newConnection();
  SSL_CTX* ssl() const { return sslCtx_; }
  void acceptOne();

 private:
  int initializeSsl(const std::string& keyFile, const std::string& certFile);
  void acceptLoop();

  int listenfd_;
  TestServerStats stats_;
  std::mutex statsLock_;
  std::unique_ptr<std::thread> acceptThread_;
  SSL_CTX* sslCtx_ = nullptr;
  struct ev_loop* loop_ = nullptr;
  ev_io listenev_;
  ev_async shutdownev_;
};

class TestConnection {
 public:
  TestConnection(TestServer* s, int fd);
  void socketLoop();
  void setBody(const absl::string_view bs);
  void setQuery(const absl::string_view qs);
  void setParseComplete() { done_ = true; }
  void setNextHeaderName(const absl::string_view n) {
    nextHeader_ = std::string(n);
  }
  void setHeaderValue(const absl::string_view v);
  std::string body() { return body_; }

 private:
  int write(const absl::string_view s);
  void sendText(int code, const absl::string_view codestr,
                const absl::string_view msg);
  void sendData(const absl::string_view msg);
  ssize_t httpTransaction(char* buf, ssize_t bufPos);
  void handleRequest();

  TestServer* server_;
  int fd_;
  bool done_ = false;
  std::string path_;
  std::unordered_map<std::string, std::string> query_;
  std::string body_;
  std::string nextHeader_;
  bool shouldClose_ = false;
  bool notAuthorized_ = false;
  int sleepTime_ = 0;
  http_parser parser_;
  SSL* ssl_ = nullptr;
};

}  // namespace apib

#endif  // APIB_TEST_SERVER_H
