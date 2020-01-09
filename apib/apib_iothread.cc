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

#include "apib/apib_iothread.h"

#include <cassert>
#include <functional>
#include <iostream>
#include <thread>

#include "absl/strings/str_join.h"
#include "apib/apib_lines.h"
#include "apib/apib_rand.h"
#include "apib/apib_reporting.h"
#include "apib/apib_time.h"
#include "apib/apib_url.h"
#include "apib/apib_util.h"
#include "ev.h"

namespace apib {

http_parser_settings IOThread::parserSettings_;
static std::once_flag parserInitalized;

ConnectionState::ConnectionState(int index, IOThread* t) : index_(index) {
  keepRunning_ = 1;
  t_ = t;
  readBuf_ = new char[kReadBufSize];
}

// TODO memory leak -- COnnectionStates are freed when threads exit but not
// when they just close. Think of a way to handle this...
ConnectionState::~ConnectionState() { delete[] readBuf_; }

int ConnectionState::httpComplete(http_parser* p) {
  ConnectionState* c = (ConnectionState*)p->data;
  c->readDone_ = 1;
  return 0;
}

void ConnectionState::writeRequest() {
  if (!writeDirty_ && (t_->oauth == nullptr)) {
    // If we're reusing the same connection with the same URL,
    // then we can re-use the write buffer
    fullWritePos_ = 0;
    return;
  }

  // Reset read buffer so that we can avoid re-allocation on every re-write
  writeBuf_.rdbuf()->pubseekpos(0, std::ios_base::in);
  writeBuf_.rdbuf()->pubseekpos(0, std::ios_base::out);
  writeBuf_ << t_->httpVerb << " " << url_->path() << " HTTP/1.1\r\n";
  if (!(t_->headersSet & IOThread::kUserAgentSet)) {
    writeBuf_ << "User-Agent: apib\r\n";
  }
  if (!(t_->headersSet & IOThread::kHostSet)) {
    writeBuf_ << "Host: " << url_->hostHeader() << "\r\n";
  }
  if (!t_->sendData.empty()) {
    if (!(t_->headersSet & IOThread::kContentTypeSet)) {
      writeBuf_ << "Content-Type: text/plain\r\n";
    }
    if (!(t_->headersSet & IOThread::kContentLengthSet)) {
      writeBuf_ << "Content-Length: " << t_->sendData.size() << "\r\n";
    }
  }
  if (t_->oauth != NULL) {
    const auto authHdr = oauth_MakeHeader(
        t_->rand(), *url_, "", t_->httpVerb.c_str(), NULL, 0, *(t_->oauth));
    writeBuf_ << authHdr << "\r\n";
  }
  if (t_->noKeepAlive && !(t_->headersSet & IOThread::kConnectionSet)) {
    writeBuf_ << "Connection: close\r\n";
  }
  if (t_->headers != nullptr) {
    for (auto it = t_->headers->begin(); it != t_->headers->end(); it++) {
      writeBuf_ << *it << "\r\n";
    }
  }
  if (t_->verbose) {
    io_Verbose(this, "%s\n", writeBuf_.str().c_str());
  }

  writeBuf_ << "\r\n";
  if (!t_->sendData.empty()) {
    writeBuf_ << t_->sendData;
  }
  if (t_->verbose) {
    io_Verbose(this, "Total send is %zi bytes\n", writeBuf_.str().size());
  }
  fullWrite_ = writeBuf_.str();
  fullWritePos_ = 0;
  writeDirty_ = false;
}

void ConnectionState::ConnectAndSend() {
  startTime_ = GetTime();
  if (needsOpen_) {
    const int err = Connect();
    if (err == 0) {
      RecordConnectionOpen();
    } else {
      std::cerr << "Error opening TCP connection: " << err << std::endl;
      RecordSocketError();
      sendAfterDelay(kConnectFailureDelay);
      return;
    }
  }
  writeRequest();
  SendWrite();
}

void ConnectionState::thinkingDone(struct ev_loop* loop, ev_timer* t,
                                   int revents) {
  ConnectionState* c = (ConnectionState*)t->data;
  io_Verbose(c, "Think time over\n");
  c->ConnectAndSend();
}

void ConnectionState::addThinkTime() {
  const double thinkTime = (double)(t_->thinkTime) / 1000.0;
  sendAfterDelay(thinkTime);
}

void ConnectionState::sendAfterDelay(double seconds) {
  io_Verbose(this, "Thinking for %.4lf seconds\n", seconds);
  ev_timer_init(&thinkTimer_, thinkingDone, seconds, 0);
  thinkTimer_.data = this;
  ev_timer_start(t_->loop(), &thinkTimer_);
}

void ConnectionState::recycle(bool closeConn) {
  if (closeConn || t_->noKeepAlive || !t_->shouldKeepRunning()) {
    needsOpen_ = true;
    // Close is async, especially for TLS. So we will
    // reconnect later...
    Close();
    return;
  }

  needsOpen_ = false;
  if (t_->thinkTime > 0) {
    addThinkTime();
  } else {
    ConnectAndSend();
  }
}

int ConnectionState::StartConnect() {
  url_ = URLInfo::GetNext(t_->rand());
  needsOpen_ = true;
  ConnectAndSend();
  return 0;
}

void ConnectionState::CloseDone() {
  if (!keepRunning_ || !t_->shouldKeepRunning()) {
    io_Verbose(this, "Connection %i closed and done\n", index_);
    return;
  }

  if (t_->thinkTime > 0) {
    addThinkTime();
  } else {
    ConnectAndSend();
  }
}

void ConnectionState::WriteDone(int err) {
  if (err != 0) {
    RecordSocketError();
    io_Verbose(this, "Error on write: %i\n", err);
    recycle(true);
  } else {
    io_Verbose(this, "Write complete. Starting to read\n");
    // Prepare to read.
    // do NOT adjust readBufPos because it may have been left over from another
    // transaction.
    readDone_ = 0;
    http_parser_init(&parser_, HTTP_RESPONSE);
    parser_.data = this;
    SendRead();
  }
}

void ConnectionState::ReadDone(int err) {
  if (err != 0) {
    io_Verbose(this, "Error on read: %i\n", err);
    RecordSocketError();
    recycle(true);
    return;
  }

  t_->recordResult(parser_.status_code, GetTime() - startTime_);
  if (!http_should_keep_alive(&(parser_))) {
    io_Verbose(this, "Server does not want keep-alive\n");
    recycle(true);
  } else {
    const URLInfo* oldUrl = url_;
    url_ = URLInfo::GetNext(t_->rand());
    if (!URLInfo::IsSameServer(*oldUrl, *url_, t_->index)) {
      io_Verbose(this, "Switching to a different server\n");
      writeDirty_ = true;
      recycle(true);
    } else {
      // URLs are static throughout the run, so we can just compare pointer here
      if (url_ != oldUrl) {
        writeDirty_ = true;
      }
      recycle(false);
    }
  }
}

void IOThread::recordResult(int statusCode, int_fast64_t latency) {
  Counters* c = getCounters();
  if ((statusCode >= 200) && (statusCode < 300)) {
    c->successfulRequests++;
  } else {
    c->failedRequests++;
  }
  c->latencies.push_back(latency);
}

void IOThread::recordRead(size_t c) { getCounters()->bytesRead += c; }

void IOThread::recordWrite(size_t c) { getCounters()->bytesWritten += c; }

void IOThread::setNumConnections(size_t newVal) {
  iothread_Verbose(this, "Current connections = %zu. New connections = %zu\n",
                   connections_.size(), newVal);
  if (newVal > connections_.size()) {
    for (size_t i = connections_.size(); i < newVal; i++) {
      iothread_Verbose(this, "Starting new connection %zu\n", i);
      ConnectionState* c = new ConnectionState(i, this);
      connections_.push_back(c);
      c->StartConnect();
    }

  } else {
    while (newVal < connections_.size()) {
      ConnectionState* last = connections_.back();
      iothread_Verbose(this, "Nicely asking connection %i to terminate\n",
                       last->index());
      last->stopRunning();
      connections_.pop_back();
      // TODO we're not freeing the connection here -- need to do that,
      // or use a shared_ptr!
    }
  }
}

void IOThread::hardShutdown(struct ev_loop* loop, ev_timer* timer,
                            int revents) {
  assert(revents & EV_TIMER);
  IOThread* t = (IOThread*)timer->data;
  iothread_Verbose(t, "Going down for a hard shutdown\n");
  ev_break(loop, EVBREAK_ALL);
}

void IOThread::processCommands(struct ev_loop* loop, ev_async* a, int revents) {
  IOThread* t = (IOThread*)a->data;
  Command cmd;
  while (t->commands_.Pop(&cmd)) {
    switch (cmd.cmd) {
      case STOP:
        iothread_Verbose(t, "Marking main loop to stop");
        t->keepRunning = 0;
        // We added this extra ref before we called ev_run
        ev_unref(t->loop_);
        // Set a timer that will fire only in case shutdown takes > 2 seconds
        ev_timer_init(&(t->shutdownTimer_), hardShutdown, cmd.stopTimeoutSecs,
                      0.0);
        t->shutdownTimer_.data = t;
        ev_timer_start(t->loop_, &(t->shutdownTimer_));
        ev_unref(t->loop_);
        break;
      case SET_CONNECTIONS:
        t->setNumConnections(cmd.newNumConnections);
        break;
      default:
        assert(0);
    }
  }
}

void IOThread::threadLoopBody() {
  int loopFlags = EVFLAG_AUTO;
  if ((numConnections < kMaxSelectFds) &&
      (ev_recommended_backends() & EVBACKEND_SELECT)) {
    loopFlags |= EVBACKEND_SELECT;
  }

  loop_ = ev_loop_new(loopFlags);
  iothread_Verbose(this, "libev backend = %s\n",
                   GetEvBackends(ev_backend(loop_)).c_str());

  // Prepare to receive async events, and be sure that we don't block if there
  // are none.
  ev_async_init(&async_, processCommands);
  async_.data = this;
  ev_async_start(loop_, &async_);
  ev_unref(loop_);

  for (int i = 0; i < numConnections; i++) {
    // First-time initialization of new connection
    ConnectionState* c = new ConnectionState(i, this);
    connections_.push_back(c);
    int err = c->StartConnect();
    if (err != 0) {
      perror("Fatal error creating non-blocking socket");
      return;
    }
  }

  // Add one more ref count so the loop will stay open even if zero connections
  if (keepRunning) {
    ev_ref(loop_);
  }
  const int ret = ev_run(loop_, 0);
  iothread_Verbose(this, "ev_run finished: %i\n", ret);
}

void IOThread::threadLoop() {
  iothread_Verbose(this, "Starting new event loop %i for %i connection\n",
                   index, numConnections);

  threadLoopBody();

  iothread_Verbose(this, "Cleaning up event loop %i\n", index);
  for (auto it = connections_.cbegin(); it != connections_.cend(); it++) {
    delete *it;
  }
  ev_loop_destroy(loop_);
}

IOThread::IOThread() {
  Counters* c = new Counters();
  counterPtr_.store(reinterpret_cast<uintptr_t>(c));
}

IOThread::~IOThread() {
  if (sslCtx != nullptr) {
    SSL_CTX_free(sslCtx);
  }
  if (thread_ != nullptr) {
    delete thread_;
  }
  Counters* c = reinterpret_cast<Counters*>(counterPtr_.load());
  delete c;
}

Counters* IOThread::exchangeCounters() {
  Counters* newCounters = new Counters();
  return reinterpret_cast<Counters*>(
      counterPtr_.exchange(reinterpret_cast<uintptr_t>(newCounters)));
}

void IOThread::initializeParser() {
  http_parser_settings_init(&parserSettings_);
  parserSettings_.on_message_complete = ConnectionState::httpComplete;
}

void IOThread::Start() {
  std::call_once(parserInitalized, initializeParser);

  // Special handling to signal thread to process only one request
  if (keepRunning < 0) {
    keepRunning = 0;
  } else {
    keepRunning = 1;
  }

  auto loopFunc = std::bind(&IOThread::threadLoop, this);
  thread_ = new std::thread(loopFunc);
}

void IOThread::RequestStop(int timeoutSecs) {
  iothread_Verbose(
      this, "Signalling to threads to stop running in less than %i seconds\n",
      timeoutSecs);

  Command cmd;
  cmd.cmd = STOP;
  cmd.stopTimeoutSecs = timeoutSecs;
  commands_.Add(cmd);
  // Wake up the loop and cause the callback to be called.
  ev_async_send(loop_, &async_);
}

void IOThread::Join() {
  if (thread_ != nullptr) {
    thread_->join();
  }
}

void IOThread::Stop() {
  RequestStop(1);
  Join();
}

void IOThread::SetNumConnections(int newConnections) {
  Command cmd;
  cmd.cmd = SET_CONNECTIONS;
  cmd.newNumConnections = newConnections;
  commands_.Add(cmd);
  // Wake up the loop and cause the callback to be called.
  ev_async_send(loop_, &async_);
}

std::string IOThread::GetEvBackends(int b) {
  std::vector<std::string> formats;
  if (b & EVBACKEND_SELECT) {
    formats.push_back("select");
  }
  if (b & EVBACKEND_POLL) {
    formats.push_back("poll");
  }
  if (b & EVBACKEND_EPOLL) {
    formats.push_back("epoll");
  }
  if (b & EVBACKEND_LINUXAIO) {
    formats.push_back("linux AIO");
  }
  if (b & EVBACKEND_KQUEUE) {
    formats.push_back("kqueue");
  }
  if (b & EVBACKEND_DEVPOLL) {
    formats.push_back("/dev/poll");
  }
  if (b & EVBACKEND_PORT) {
    formats.push_back("Solaris event port");
  }
  return absl::StrJoin(formats, ", ");
}

}  // namespace apib