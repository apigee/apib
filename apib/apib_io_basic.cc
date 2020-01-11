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

#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <openssl/err.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include <cassert>
#include <iostream>

#include "apib/apib_iothread.h"

namespace apib {

int ConnectionState::Connect() {
  const Address addr = url_->address(t_->threadIndex());
  if (!addr.valid()) {
    io_Verbose(this, "No addresses to look up\n");
    return -1;
  }

  if (t_->verbose) {
    io_Verbose(this, "Connecting to %s. (TLS = %i)\n", addr.str().c_str(),
               url_->isSsl());
  }
  std::unique_ptr<Socket> sock;
  Status connectStatus;
  if (url_->isSsl()) {
    TLSSocket* ts = new TLSSocket();
    connectStatus = ts->connectTLS(addr, url_->hostName(), t_->sslCtx);
    sock.reset(ts);
  } else {
    Socket* ps = new Socket();
    connectStatus = ps->connect(addr);
    sock.reset(ps);
  }

  if (!connectStatus.ok()) {
    io_Verbose(this, "Error on connect: %s\n", connectStatus.str().c_str());
    return -1;
  }

  socket_.swap(sock);
  return 0;
}

void ConnectionState::completeShutdown(struct ev_loop* loop, ev_io* w,
                                       int revents) {
  // We only get here for TLS. We already sent a shutdown message,
  // so really all we have to do is read with empty buffers.
  ConnectionState* c = (ConnectionState*)(w->data);
  io_Verbose(c, "I/O ready on shutdown path: %i\n", revents);

  size_t readed;
  const auto rs = c->socket_->read(NULL, 0, &readed);
  if (!rs.ok()) {
    io_Verbose(c, "Shutdown finished with error: %s\n",
               rs.status().str().c_str());
    ev_io_stop(loop, &(c->io_));
    c->socket_.release();
    c->CloseDone();
    return;
  }

  switch (rs.value()) {
    case OK:
    case FEOF:
      io_Verbose(c, "Close complete\n");
      ev_io_stop(loop, &(c->io_));
      c->socket_.release();
      c->CloseDone();
      break;
    case NEED_READ:
      io_Verbose(c, "Close needs read\n");
      if (c->backwardsIo_) {
        ev_io_stop(loop, &(c->io_));
        ev_io_set(&(c->io_), c->socket_->fd(), EV_READ);
        c->backwardsIo_ = false;
        ev_io_start(loop, &(c->io_));
      }
      break;
    case NEED_WRITE:
      io_Verbose(c, "Close needs write\n");
      if (!c->backwardsIo_) {
        ev_io_stop(loop, &(c->io_));
        ev_io_set(&(c->io_), c->socket_->fd(), EV_WRITE);
        c->backwardsIo_ = true;
        ev_io_start(loop, &(c->io_));
      }
      break;
  }
}

void ConnectionState::Close() {
  const auto cs = socket_->close();
  if (!cs.ok()) {
    io_Verbose(this, "Close finished with error: %s\n", cs.str().c_str());
    ev_io_stop(t_->loop(), &io_);
    socket_.release();
    CloseDone();
    return;
  }

  switch (cs.value()) {
    case OK:
    case FEOF:
      io_Verbose(this, "Close complete\n");
      socket_.release();
      CloseDone();
      break;
    case NEED_READ:
      io_Verbose(this, "Close needs read\n");
      ev_init(&io_, completeShutdown);
      ev_io_set(&io_, socket_->fd(), EV_READ);
      backwardsIo_ = false;
      io_.data = this;
      ev_io_start(t_->loop(), &io_);
      break;
    case NEED_WRITE:
      io_Verbose(this, "Close needs write\n");
      ev_init(&io_, completeShutdown);
      ev_io_set(&io_, socket_->fd(), EV_WRITE);
      backwardsIo_ = true;
      io_.data = this;
      ev_io_start(t_->loop(), &io_);
      break;
  }
}

// Called by libev whenever the socket is ready for writing.
// -1 means write is done.
// 0 means write would block
// 1 means keep on writing
int ConnectionState::singleWrite(struct ev_loop* loop, ev_io* w, int revents) {
  io_Verbose(this, "I/O ready on write path: %i\n", revents);

  const size_t len = fullWrite_.size() - fullWritePos_;
  assert(len > 0);
  size_t wrote;
  const auto writeStatus =
      socket_->write(fullWrite_.data() + fullWritePos_, len, &wrote);

  if (!writeStatus.ok()) {
    io_Verbose(this, "Error on write: %s\n", writeStatus.str().c_str());
    ev_io_stop(loop, &io_);
    WriteDone(-1);
    return -1;
  }

  switch (writeStatus.value()) {
    case OK:
      io_Verbose(this, "Successfully wrote %zu bytes\n", wrote);
      fullWritePos_ += wrote;
      t_->recordWrite(wrote);
      if (fullWritePos_ == fullWrite_.size()) {
        // Whole message body has been written, so stop writing
        ev_io_stop(loop, &io_);
        WriteDone(0);
        return 0;
      }
      return 1;
    case FEOF:
      io_Verbose(this, "write wrote zero bytes. Ignoring.\n");
      return 1;
    case NEED_WRITE:
      io_Verbose(this, "I/O would block on writing\n");
      if (backwardsIo_) {
        io_Verbose(this, "Restoring I/O direction\n");
        backwardsIo_ = false;
        ev_io_stop(loop, &io_);
        ev_io_set(&io_, socket_->fd(), EV_WRITE);
        ev_io_start(loop, &io_);
      }
      return 0;
    case NEED_READ:
      io_Verbose(this, "I/O would block on read while writing\n");
      if (!backwardsIo_) {
        io_Verbose(this, "Switching I/O direction\n");
        backwardsIo_ = true;
        ev_io_stop(loop, &io_);
        ev_io_set(&io_, socket_->fd(), EV_READ);
        ev_io_start(loop, &io_);
      }
      return 0;
    default:
      assert(0);
      return -1;
  }
}

void ConnectionState::writeReady(struct ev_loop* loop, ev_io* w, int revents) {
  ConnectionState* c = (ConnectionState*)w->data;
  for (int keepOnWriting = 1; keepOnWriting > 0;
       keepOnWriting = c->singleWrite(loop, w, revents))
    ;
}

// Set up libev to asychronously write from writeBuf
void ConnectionState::SendWrite() {
  ev_init(&io_, writeReady);
  ev_io_set(&io_, socket_->fd(), EV_WRITE);
  backwardsIo_ = false;
  io_.data = this;
  ev_io_start(t_->loop(), &io_);
}

int ConnectionState::singleRead(struct ev_loop* loop, ev_io* w, int revents) {
  io_Verbose(this, "I/O ready on read path: %i\n", revents);
  const size_t len = kReadBufSize - readBufPos_;
  assert(len > 0);

  size_t readCount;
  const auto readStatus =
      socket_->read(readBuf_ + readBufPos_, len, &readCount);

  if (!readStatus.ok()) {
    // Read error. Stop going.
    io_Verbose(this, "Error reading from socket: %s\n",
               readStatus.str().c_str());
    ev_io_stop(loop, &io_);
    ReadDone(-3);
    return -1;
  }

  if (readStatus.value() == OK) {
    io_Verbose(this, "Successfully read %zu bytes\n", readCount);
    t_->recordRead(readCount);
    // Parse the data we just read plus whatever was left from before
    const size_t parsedLen = readCount + readBufPos_;

    if (t_->verbose) {
      fwrite(readBuf_, parsedLen, 1, stdout);
    }

    const size_t parsed = http_parser_execute(&parser_, t_->parserSettings(),
                                              readBuf_, parsedLen);
    io_Verbose(this, "Parsed %zu\n", parsed);
    if (parser_.http_errno != 0) {
      // Invalid HTTP response. Complete with an error.
      io_Verbose(this, "Parsing error %i\n", parser_.http_errno);
      ev_io_stop(t_->loop(), &io_);
      ReadDone(-1);
      return -1;
    }

    if (parsed < parsedLen) {
      // http_parser didn't have enough data, and only parsed part of it.
      // Move the unparsed data down to the start of the buffer so that
      // we can put new data after it
      const size_t unparsedLen = parsedLen - parsed;
      memmove(readBuf_, readBuf_ + parsed, unparsedLen);
      readBufPos_ = unparsedLen;
    } else {
      readBufPos_ = 0;
    }

    // "readDone" set by an http_parser callback that's set up on
    // apib_iothread.c
    if (readDone_) {
      // Parser parsed all the content, so we're done for now.
      ev_io_stop(loop, &io_);
      ReadDone(0);
      return 0;
    }
    return 1;
  }

  if (readStatus.value() == FEOF) {
    io_Verbose(this, "EOF. Done = %i\n", readDone_);
    ev_io_stop(loop, &io_);
    ReadDone(readDone_ ? 0 : -2);
    return 0;
  }

  if (readStatus.value() == NEED_READ) {
    io_Verbose(this, "I/O would block on read\n");
    if (backwardsIo_) {
      io_Verbose(this, "Restoring I/O direction\n");
      backwardsIo_ = 0;
      ev_io_stop(loop, &io_);
      ev_io_set(&io_, socket_->fd(), EV_READ);
      ev_io_start(loop, &io_);
    }
    return 0;
  }

  if (readStatus.value() == NEED_WRITE) {
    io_Verbose(this, "I/O would block on write while reading\n");
    if (!backwardsIo_) {
      io_Verbose(this, "Switching I/O direction\n");
      backwardsIo_ = 1;
      ev_io_stop(loop, &io_);
      ev_io_set(&io_, socket_->fd(), EV_WRITE);
      ev_io_start(loop, &io_);
    }
    return 0;
  }

  assert(0);
  return -1;
}

void ConnectionState::readReady(struct ev_loop* loop, ev_io* w, int revents) {
  ConnectionState* c = (ConnectionState*)w->data;
  for (int keepReading = 1; keepReading > 0;
       keepReading = c->singleRead(loop, w, revents))
    ;
}

// Set up libev to asynchronously read to readBuf
void ConnectionState::SendRead() {
  ev_init(&io_, readReady);
  ev_io_set(&io_, socket_->fd(), EV_READ);
  io_.data = this;
  backwardsIo_ = 0;
  ev_io_start(t_->loop(), &io_);
}

}  // namespace apib