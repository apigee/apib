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

#include "src/apib_iothread.h"

namespace apib {

void ConnectionState::printSslError(const std::string& msg, int err) const {
  char buf[256];
  ERR_error_string_n(err, buf, 256);
  std::cerr << buf << ": " << msg << std::endl;
}

int ConnectionState::Connect() {
  fd_ = socket(AF_INET, SOCK_STREAM, 0);
  assert(fd_ > 0);

  int yes = 1;
  // Set NODELAY to minimize latency and because we are not processing
  // keystrokes
  int err = setsockopt(fd_, IPPROTO_TCP, TCP_NODELAY, &yes, sizeof(int));
  assert(err == 0);
  // Set REUSEADDR for convenience when running lots of tests
  err = setsockopt(fd_, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int));
  assert(err == 0);

  struct linger linger;
  linger.l_onoff = 0;
  linger.l_linger = 0;
  // Disable LINGER so that we don't run out of sockets when testing with
  // keep-alive disabled. (We'll still need kernel settings often too.)
  err = setsockopt(fd_, SOL_SOCKET, SO_LINGER, &linger, sizeof(struct linger));
  assert(err == 0);

  // The socket must be non-blocking or the rest of the logic doesn't work.
  err = fcntl(fd_, F_SETFL, O_NONBLOCK);
  assert(err == 0);

  io_Verbose(this, "Made new TCP connection %i\n", fd_);

  size_t addrLen;
  const struct sockaddr* addr = url_->address(t_->threadIndex(), &addrLen);

  if (t_->verbose) {
    char hostName[512];
    getnameinfo(addr, addrLen, hostName, 512, nullptr, 0, NI_NUMERICHOST);
    io_Verbose(this, "Connecting to %s:%i\n", hostName,
               ntohs(((struct sockaddr_in*)addr)->sin_port));
  }

  err = connect(fd_, addr, addrLen);
  if ((err != 0) && (errno == EINPROGRESS)) {
    err = 0;
  } else if (err != 0) {
    io_Verbose(this, "Error from connect(): %i\n", errno);
  }

  if ((err == 0) && (url_->isSsl())) {
    io_Verbose(this, "Setting up connection for TLS\n");
    assert(t_->sslCtx != NULL);
    ssl_ = SSL_new(t_->sslCtx);
    if (ssl_ == NULL) {
      printSslError("Can't create new SSL", ERR_get_error());
      return -2;
    }
    int sslErr = SSL_set_fd(ssl_, fd_);
    if (sslErr != 1) {
      printSslError("Can't initialize SSL connection", sslErr);
      return -2;
    }
    sslErr = SSL_set_tlsext_host_name(ssl_, url_->hostName().c_str());
    if (sslErr != 1) {
      printSslError("Can't set host name on SSL connection", sslErr);
      return -3;
    }
    SSL_set_connect_state(ssl_);
  }
  return err;
}

void ConnectionState::completeShutdown(struct ev_loop* loop, ev_io* w,
                                       int revents) {
  // We only get here for TLS. We already sent a shutdown message,
  // so really all we have to do is read with empty buffers.
  ConnectionState* c = (ConnectionState*)(w->data);
  io_Verbose(c, "I/O ready on shutdown path: %i\n", revents);

  size_t readed;
  const IOStatus s = c->doRead(NULL, 0, &readed);

  switch (s) {
    case OK:
    case FEOF:
    case SOCKET_ERROR:
    case TLS_ERROR:
      io_Verbose(c, "Close complete\n");
      ev_io_stop(loop, &(c->io_));
      c->Reset();
      c->CloseDone();
      break;
    case NEED_READ:
      io_Verbose(c, "Close needs read\n");
      if (c->backwardsIo_) {
        ev_io_stop(loop, &(c->io_));
        ev_io_set(&(c->io_), c->fd_, EV_READ);
        c->backwardsIo_ = false;
        ev_io_start(loop, &(c->io_));
      }
      break;
    case NEED_WRITE:
      io_Verbose(c, "Close needs write\n");
      if (!c->backwardsIo_) {
        ev_io_stop(loop, &(c->io_));
        ev_io_set(&(c->io_), c->fd_, EV_WRITE);
        c->backwardsIo_ = true;
        ev_io_start(loop, &(c->io_));
      }
      break;
  }
}

void ConnectionState::Close() {
  const IOStatus s = doClose();
  switch (s) {
    case OK:
    case FEOF:
    case SOCKET_ERROR:
    case TLS_ERROR:
      io_Verbose(this, "Close complete\n");
      Reset();
      CloseDone();
      break;
    case NEED_READ:
      io_Verbose(this, "Close needs read\n");
      ev_init(&io_, completeShutdown);
      ev_io_set(&io_, fd_, EV_READ);
      backwardsIo_ = false;
      io_.data = this;
      ev_io_start(t_->loop(), &io_);
      break;
    case NEED_WRITE:
      io_Verbose(this, "Close needs write\n");
      ev_init(&io_, completeShutdown);
      ev_io_set(&io_, fd_, EV_WRITE);
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
  const IOStatus writeStatus =
      doWrite(fullWrite_.data() + fullWritePos_, len, &wrote);

  switch (writeStatus) {
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
    case SOCKET_ERROR:
    case TLS_ERROR:
      io_Verbose(this, "Error on write");
      ev_io_stop(loop, &io_);
      WriteDone(-1);
      return -1;
    case NEED_WRITE:
      io_Verbose(this, "I/O would block on writing\n");
      if (backwardsIo_) {
        io_Verbose(this, "Restoring I/O direction\n");
        backwardsIo_ = false;
        ev_io_stop(loop, &io_);
        ev_io_set(&io_, fd_, EV_WRITE);
        ev_io_start(loop, &io_);
      }
      return 0;
    case NEED_READ:
      io_Verbose(this, "I/O would block on read while writing\n");
      if (!backwardsIo_) {
        io_Verbose(this, "Switching I/O direction\n");
        backwardsIo_ = true;
        ev_io_stop(loop, &io_);
        ev_io_set(&io_, fd_, EV_READ);
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
  ev_io_set(&io_, fd_, EV_WRITE);
  backwardsIo_ = false;
  io_.data = this;
  ev_io_start(t_->loop(), &io_);
}

int ConnectionState::singleRead(struct ev_loop* loop, ev_io* w, int revents) {
  io_Verbose(this, "I/O ready on read path: %i\n", revents);
  const size_t len = readBufSize - readBufPos_;
  assert(len > 0);

  size_t readCount;
  const IOStatus readStatus = doRead(readBuf_ + readBufPos_, len, &readCount);

  if (readStatus == OK) {
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

  if (readStatus == FEOF) {
    io_Verbose(this, "EOF. Done = %i\n", readDone_);
    ev_io_stop(loop, &io_);
    ReadDone(readDone_ ? 0 : -2);
    return 0;
  }

  if ((readStatus == SOCKET_ERROR) || (readStatus == TLS_ERROR)) {
    // Read error. Stop going.
    io_Verbose(this, "Error reading from socket\n");
    ev_io_stop(loop, &io_);
    ReadDone(-3);
    return -1;
  }

  if (readStatus == NEED_READ) {
    io_Verbose(this, "I/O would block on read\n");
    if (backwardsIo_) {
      io_Verbose(this, "Restoring I/O direction\n");
      backwardsIo_ = 0;
      ev_io_stop(loop, &io_);
      ev_io_set(&io_, fd_, EV_READ);
      ev_io_start(loop, &io_);
    }
    return 0;
  }

  if (readStatus == NEED_WRITE) {
    io_Verbose(this, "I/O would block on write while reading\n");
    if (!backwardsIo_) {
      io_Verbose(this, "Switching I/O direction\n");
      backwardsIo_ = 1;
      ev_io_stop(loop, &io_);
      ev_io_set(&io_, fd_, EV_WRITE);
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
  ev_io_set(&io_, fd_, EV_READ);
  io_.data = this;
  backwardsIo_ = 0;
  ev_io_start(t_->loop(), &io_);
}

}  // namespace apib