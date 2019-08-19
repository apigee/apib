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
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include "src/apib_iothread.h"

int io_Connect(ConnectionState* c) {
  c->fd = socket(AF_INET, SOCK_STREAM, 0);
  assert(c->fd > 0);

  int yes = 1;
  // Set NODELAY to minimize latency and because we are not processing
  // keystrokes
  int err = setsockopt(c->fd, IPPROTO_TCP, TCP_NODELAY, &yes, sizeof(int));
  assert(err == 0);
  // Set REUSEADDR for convenience when running lots of tests
  err = setsockopt(c->fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int));
  assert(err == 0);

  struct linger linger;
  linger.l_onoff = 0;
  linger.l_linger = 0;
  // Disable LINGER so that we don't run out of sockets when testing with
  // keep-alive disabled. (We'll still need kernel settings often too.)
  err =
      setsockopt(c->fd, SOL_SOCKET, SO_LINGER, &linger, sizeof(struct linger));
  assert(err == 0);

  // The socket must be non-blocking or the rest of the logic doesn't work.
  err = fcntl(c->fd, F_SETFL, O_NONBLOCK);
  assert(err == 0);

  io_Verbose(c, "Made new TCP connection %i\n", c->fd);

  size_t addrLen;
  const struct sockaddr* addr = url_GetAddress(c->url, c->t->index, &addrLen);
  err = connect(c->fd, addr, addrLen);
  if ((err != 0) && (errno == EINPROGRESS)) {
    return 0;
  }
  return err;
}

void io_Close(ConnectionState* c) { 
  io_Verbose(c, "Closing connection %i\n", c->fd);
  close(c->fd);
}

// Called by libev whenever the socket is ready for writing.
static void writeReady(struct ev_loop* loop, ev_io* w, int revents) {
  assert(revents | EV_WRITE);
  ConnectionState* c = (ConnectionState*)w->data;

  const size_t len = buf_Length(&(c->writeBuf)) - c->writeBufPos;
  assert(len > 0);
  const ssize_t wrote =
      write(c->fd, buf_Get(&(c->writeBuf)) + c->writeBufPos, len);
  int writeErr = errno;
  io_Verbose(c, "Tried to write %u bytes: result %i\n", len, wrote);
  if (wrote > 0) {
    c->writeBufPos += wrote;
    c->t->writeBytes += wrote;
  } else if (wrote < 0) {
    io_Verbose(c, "Write error: %i\n", writeErr);
    if ((writeErr != EAGAIN) && (writeErr != EWOULDBLOCK)) {
      // Write error: return to caller and stop getting write events
      ev_io_stop(c->t->loop, &(c->io));
      io_WriteDone(c, -1);
      return;
    }
    // If we get here, then just exit -- libev will call this
    // function again when the socket is ready for more writing
    io_Verbose(c, "I/O would block\n");
  }

  if (c->writeBufPos == buf_Length(&(c->writeBuf))) {
    // Whole message body has been written, so stop writing
    ev_io_stop(c->t->loop, &(c->io));
    io_WriteDone(c, 0);
  }
}

// Set up libev to asychronously write from writeBuf
void io_SendWrite(ConnectionState* c) {
  ev_init(&(c->io), writeReady);
  ev_io_set(&(c->io), c->fd, EV_WRITE);
  c->io.data = c;
  ev_io_start(c->t->loop, &(c->io));
}

static void readReady(struct ev_loop* loop, ev_io* w, int revents) {
  assert(revents | EV_READ);
  ConnectionState* c = (ConnectionState*)w->data;

  const size_t len = READ_BUF_SIZE - c->readBufPos;
  assert(len > 0);
  const ssize_t readCount = read(c->fd, c->readBuf + c->readBufPos, len);
  io_Verbose(c, "Tried to read %u bytes: result %i\n", len, readCount);

  if (readCount > 0) {
    c->t->readBytes += readCount;
    // Parse the data we just read plus whatever was left from before
    const size_t parsedLen = readCount + c->readBufPos;
    const size_t parsed = http_parser_execute(&(c->parser), &HttpParserSettings,
                                              c->readBuf, parsedLen);
    io_Verbose(c, "Parsed %u\n", parsed);
    if (c->parser.http_errno != 0) {
      // Invalid HTTP response. Complete with an error.
      io_Verbose(c, "Parsing error %i\n", c->parser.http_errno);
      ev_io_stop(c->t->loop, &(c->io));
      io_ReadDone(c, -1);
      return;
    }

    if (parsed < parsedLen) {
      // http_parser didn't have enough data, and only parsed part of it.
      // Move the unparsed data down to the start of the buffer so that
      // we can put new data after it
      const size_t unparsedLen = parsedLen - parsed;
      memmove(c->readBuf, c->readBuf + parsed, unparsedLen);
      c->readBufPos = unparsedLen;
    } else {
      c->readBufPos = 0;
    }

    // "readDone" set by an http_parser callback that's set up on
    // apib_iothread.c
    if (c->readDone) {
      // Parser parsed all the content, so we're done for now.
      ev_io_stop(c->t->loop, &(c->io));
      io_ReadDone(c, 0);
    }

  } else if (readCount == 0) {
    // EOF. We might have read everything, or might not.
    io_Verbose(c, "EOF. Done = %i\n", c->readDone);
    ev_io_stop(c->t->loop, &(c->io));
    io_ReadDone(c, c->readDone ? 0 : -2);

  } else if (readCount < 0) {
    if ((errno != EAGAIN) && (errno != EWOULDBLOCK)) {
      // Read error. Stop going.
      io_Verbose(c, "Error reading from socket: %i\n", errno);
      ev_io_stop(c->t->loop, &(c->io));
      io_ReadDone(c, -3);
      return;
    }
    // If we get here, then libev will call this function again when there
    // is more data to read.
    io_Verbose(c, "I/O would block\n");
  }
}

// Set up libev to asynchronously read to readBuf
void io_SendRead(ConnectionState* c) {
  ev_init(&(c->io), readReady);
  ev_io_set(&(c->io), c->fd, EV_READ);
  c->io.data = c;
  ev_io_start(c->t->loop, &(c->io));
}