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

#include "src/apib_iothread.h"

#include <cassert>
#include <functional>
#include <thread>

#include "ev.h"
#include "src/apib_lines.h"
#include "src/apib_rand.h"
#include "src/apib_reporting.h"
#include "src/apib_time.h"
#include "src/apib_url.h"
#include "src/apib_util.h"

namespace apib {

http_parser_settings IOThread::parserSettings_;
static std::once_flag parserInitalized;

ConnectionState::ConnectionState(int index, IOThread* t) {
  index_ = index;
  keepRunning_ = 1;
  t_ = t;
  readBuf_ = (char*)malloc(readBufSize);
}

// TODO memory leak -- COnnectionStates are freed when threads exit but not
// when they just close. Think of a way to handle this...
ConnectionState::~ConnectionState() { free(readBuf_); }

int ConnectionState::httpComplete(http_parser* p) {
  ConnectionState* c = (ConnectionState*)p->data;
  c->readDone_ = 1;
  return 0;
}

void ConnectionState::writeRequest() {
  // TODO if we didn't change URL, then don't do this every time!
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
}

void ConnectionState::ConnectAndSend() {
  startTime_ = GetTime();
  if (needsOpen_) {
    int err = Connect();
    mandatoryAssert(err == 0);
    // Should only fail if we can't create a new socket --
    // errors actually connecting will be handled during write.
    RecordConnectionOpen();
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
  io_Verbose(this, "Thinking for %.4lf seconds\n", thinkTime);
  ev_timer_init(&thinkTimer_, thinkingDone, thinkTime, 0);
  thinkTimer_.data = this;
  ev_timer_start(t_->loop(), &thinkTimer_);
}

void ConnectionState::recycle(int closeConn) {
  if (closeConn || t_->noKeepAlive || !t_->shouldKeepRunning()) {
    needsOpen_ = 1;
    // Close is async, especially for TLS. So we will
    // reconnect later...
    Close();
    return;
  }

  needsOpen_ = 0;
  if (t_->thinkTime > 0) {
    addThinkTime();
  } else {
    ConnectAndSend();
  }
}

int ConnectionState::StartConnect() {
  url_ = URLInfo::GetNext(t_->rand());
  needsOpen_ = 1;
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
    recycle(1);
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
    recycle(1);
    return;
  }

  RecordResult(parser_.status_code);
  t_->recordLatency(GetTime() - startTime_);
  if (!http_should_keep_alive(&(parser_))) {
    io_Verbose(this, "Server does not want keep-alive\n");
    recycle(1);
  } else {
    const URLInfo* oldUrl = url_;
    url_ = URLInfo::GetNext(t_->rand());
    if (!URLInfo::IsSameServer(*oldUrl, *url_, t_->index)) {
      io_Verbose(this, "Switching to a different server\n");
      recycle(1);
    } else {
      recycle(0);
    }
  }
}

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

void IOThread::threadLoop() {
  int ret = 0;
  readCount_ = 0;
  writeCount_ = 0;
  readBytes_ = 0;
  writeBytes_ = 0;

  iothread_Verbose(this, "Starting new event loop %i for %i connection\n",
                   index, numConnections);

  loop_ = ev_loop_new(EVFLAG_AUTO);
  iothread_Verbose(this, "libev backend = %i\n", ev_backend(loop_));
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
      perror("Error creating non-blocking socket");
      goto finish;
    }
  }

  // Add one more ref count so the loop will stay open even if zero connections
  if (keepRunning) {
    ev_ref(loop_);
  }
  ret = ev_run(loop_, 0);
  iothread_Verbose(this, "ev_run finished: %i\n", ret);
  RecordByteCounts(writeBytes_, readBytes_);
  RecordLatencies(latencies_);

finish:
  iothread_Verbose(this, "Cleaning up event loop %i\n", index);
  for (auto it = connections_.cbegin(); it != connections_.cend(); it++) {
    delete *it;
  }
  ev_loop_destroy(loop_);
}

IOThread::~IOThread() {
  if (sslCtx != nullptr) {
    SSL_CTX_free(sslCtx);
  }
  if (thread_ != nullptr) {
    delete thread_;
  }
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

void IOThread::recordRead(size_t c) {
  readCount_++;
  readBytes_ += c;
}

void IOThread::recordWrite(size_t c) {
  writeCount_++;
  writeBytes_ += c;
}

}  // namespace apib