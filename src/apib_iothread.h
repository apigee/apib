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

#ifndef APIB_IOTHREAD_H
#define APIB_IOTHREAD_H

#include <openssl/ssl.h>

#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "ev.h"
#include "http_parser.h"
#include "src/apib_commandqueue.h"
#include "src/apib_lines.h"
#include "src/apib_oauth.h"
#include "src/apib_rand.h"
#include "src/apib_url.h"

namespace apib {

// Constants used to keep track of which headers were
// already set

class ConnectionState;

// This structure represents a single thread that runs a benchmark
// across multiple connections.
class IOThread {
 public:
  // The caller must set these directly to configure the thread
  int index = 0;
  int numConnections = 0;
  bool verbose = false;
  std::string httpVerb;
  std::string sslCipher;
  std::string sendData;
  SSL_CTX* sslCtx = nullptr;
  OAuthInfo* oauth = nullptr;
  std::vector<std::string>* headers = nullptr;
  int headersSet = 0;
  unsigned int thinkTime = 0;
  int noKeepAlive = 0;
  int keepRunning = 0;
  // Everything ABOVE must be initialized.

  // Constants for "headersSet"
  static constexpr int kHostSet = (1 << 0);
  static constexpr int kContentLengthSet = (1 << 1);
  static constexpr int kContentTypeSet = (1 << 2);
  static constexpr int kAuthorizationSet = (1 << 3);
  static constexpr int kConnectionSet = (1 << 4);
  static constexpr int kUserAgentSet = (1 << 5);

  ~IOThread();

  // Start the thread. It's up to the caller to initialize everything
  // in the structure above. This call will spawn a thread, and keep
  // running until "Stop" is called.
  void Start();

  // Stop the thread. It will signal for a stop, and then stop
  // more forcefully after "timeoutSecs" seconds
  void RequestStop(int timeoutSecs);

  // Wait for the thread to exit cleanly.
  void Join();

  // Convenience that stops and joins all at once with a one-second timeout
  void Stop();

  // Change the number of connections. This will happen as part of normal
  // processing, with unneeded connections shutting down when done
  // with their current requests.
  void SetNumConnections(int newConnections);

  struct ev_loop* loop() {
    return loop_;
  }
  int threadIndex() { return index; }
  http_parser_settings* parserSettings() { return &parserSettings_; }
  bool shouldKeepRunning() { return keepRunning; }
  RandState randState() { return randState_; }

  void recordRead(size_t c);
  void recordWrite(size_t c);

 private:
  void threadLoop();
  static void initializeParser();
  static void processCommands(struct ev_loop* loop, ev_async* a, int revents);
  static void hardShutdown(struct ev_loop* loop, ev_timer* timer, int revents);
  void setNumConnections(size_t newVal);

  static http_parser_settings parserSettings_;

  long readCount_;
  long writeCount_;
  long long readBytes_;
  long long writeBytes_;
  std::vector<ConnectionState*> connections_;
  std::thread* thread_;
  RandState randState_;
  struct ev_loop* loop_;
  ev_async async_;
  CommandQueue commands_;
  ev_timer shutdownTimer_;
};

typedef enum {
  OK,
  NEED_READ,
  NEED_WRITE,
  FEOF,
  TLS_ERROR,
  SOCKET_ERROR
} IOStatus;

// This is an internal class used per connection.
class ConnectionState {
 public:
  ConnectionState(int index, IOThread* t);
  ~ConnectionState();

  // Called when asynchronous I/O completes
  void WriteDone(int err);
  void ReadDone(int err);
  void CloseDone();

  // Connect in a non-blocking way, and return non-zero on error.
  int Connect();
  void ConnectAndSend();
  int StartConnect();

  // Write what's in "sendBuf" to the socket, and call io_WriteDone when done.
  void SendWrite();

  // Read the whole HTTP response and call "io_ReadDone" when done.
  void SendRead();

  // Do what it says on the tin, and call "CloseDone" when done.
  void Close();

  // Reset internal state so that the connection can be opened again
  void Reset();

  int index() const { return index_; }
  void stopRunning() { keepRunning_ = 0; }
  static int httpComplete(http_parser* p);

 private:
  static const int readBufSize = 512;
  static const int writeBufSize = 1024;

  void addThinkTime();
  void recycle(int closeConn);
  void writeRequest();
  void printSslError(const std::string& msg, int err) const;

  IOStatus doWrite(const void* buf, size_t count, size_t* written);
  IOStatus doRead(void* buf, size_t count, size_t* readed);
  IOStatus doClose();

  int singleRead(struct ev_loop* loop, ev_io* w, int revents);
  int singleWrite(struct ev_loop* loop, ev_io* w, int revents);

  static void completeShutdown(struct ev_loop* loop, ev_io* w, int revents);
  static void readReady(struct ev_loop* loop, ev_io* w, int revents);
  static void writeReady(struct ev_loop* loop, ev_io* w, int revents);
  static void thinkingDone(struct ev_loop* loop, ev_timer* t, int revents);

  int index_ = 0;
  bool keepRunning_ = 0;
  IOThread* t_ = nullptr;
  int fd_ = 0;
  SSL* ssl_ = nullptr;
  bool backwardsIo_ = false;
  ev_io io_;
  ev_timer thinkTimer_;
  URLInfo* url_ = nullptr;
  std::ostringstream writeBuf_;
  std::string fullWrite_;
  size_t fullWritePos_ = 0;
  char* readBuf_ = nullptr;
  size_t readBufPos_ = 0;
  http_parser parser_;
  bool readDone_ = false;
  bool needsOpen_ = false;
  long long startTime_ = 0LL;
};

// Debugging macro
#define io_Verbose(c, ...) \
  if ((c)->t_->verbose) {  \
    printf(__VA_ARGS__);   \
  }

#define iothread_Verbose(t, ...) \
  if ((t)->verbose) {            \
    printf(__VA_ARGS__);         \
  }

}  // namespace apib

#endif  // APIB_IOTHREAD_H