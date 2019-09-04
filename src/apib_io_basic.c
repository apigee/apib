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
#include <openssl/err.h>
#include <openssl/ssl.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include "src/apib_iothread.h"

static void printSslError(const char* msg) {
  char buf[256];
  ERR_error_string_n(ERR_get_error(), buf, 256);
  fprintf(stderr, "%s: %s\n", msg, buf);
}

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
    err = 0;
  }

  if ((err == 0) && (c->url->isSsl)) {
    io_Verbose(c, "Setting up connection for TLS\n");
    assert(c->t->sslCtx != NULL);
    c->ssl = SSL_new(c->t->sslCtx);
    if (c->ssl == NULL) {
      printSslError("Can't create new SSL");
      return -2;
    }
    int sslErr = SSL_set_fd(c->ssl, c->fd);
    if (sslErr != 1) {
      printSslError("Can't initialize SSL connection");
      return -2;
    }
    sslErr = SSL_set_tlsext_host_name(c->ssl, c->url->hostName);
    if (sslErr != 1) {
      printSslError("Can't set host name on SSL connection");
      return -3;
    }
    SSL_set_connect_state(c->ssl);
  }
  return err;
}

static void completeShutdown(struct ev_loop* loop, ev_io* w, int revents) {
  // We only get here for TLS. We already sent a shutdown message,
  // so really all we have to do is read with empty buffers.
  ConnectionState* c = (ConnectionState*)(w->data);
  io_Verbose(c, "I/O ready on shutdown path: %i\n", revents);

  size_t readed;
  const IOStatus s = io_Read(c, NULL, 0, &readed);

  switch (s) {
    case OK:
    case FEOF:
    case SOCKET_ERROR:
    case TLS_ERROR:
      io_Verbose(c, "Close complete\n");
      ev_io_stop(c->t->loop, &(c->io));
      io_FreeConnection(c);
      io_CloseDone(c);
      break;
    case NEED_READ:
      io_Verbose(c, "Close needs read\n");
      if (c->backwardsIo) {
        ev_io_stop(c->t->loop, &(c->io));
        ev_io_set(&(c->io), c->fd, EV_READ);
        c->backwardsIo = 0;
        ev_io_start(c->t->loop, &(c->io));
      }
      break;
    case NEED_WRITE:
      io_Verbose(c, "Close needs write\n");
      if (!c->backwardsIo) {
        ev_io_stop(c->t->loop, &(c->io));
        ev_io_set(&(c->io), c->fd, EV_WRITE);
        c->backwardsIo = 1;
        ev_io_start(c->t->loop, &(c->io));
      }
      break;
  }
}

void io_Close(ConnectionState* c) {
  const IOStatus s = io_CloseConnection(c);
  switch (s) {
    case OK:
    case FEOF:
    case SOCKET_ERROR:
    case TLS_ERROR:
      io_Verbose(c, "Close complete\n");
      io_FreeConnection(c);
      io_CloseDone(c);
      break;
    case NEED_READ:
      io_Verbose(c, "Close needs read\n");
      ev_init(&(c->io), completeShutdown);
      ev_io_set(&(c->io), c->fd, EV_READ);
      c->backwardsIo = 0;
      c->io.data = c;
      ev_io_start(c->t->loop, &(c->io));
      break;
    case NEED_WRITE:
      io_Verbose(c, "Close needs write\n");
      ev_init(&(c->io), completeShutdown);
      ev_io_set(&(c->io), c->fd, EV_WRITE);
      c->backwardsIo = 1;
      c->io.data = c;
      ev_io_start(c->t->loop, &(c->io));
      break;
  }
}

// Called by libev whenever the socket is ready for writing.
// -1 means write is done.
// 0 means write would block
// 1 means keep on writing
static int singleWrite(ConnectionState* c, struct ev_loop* loop, ev_io* w,
                       int revents) {
  io_Verbose(c, "I/O ready on write path: %i\n", revents);
  const size_t len = buf_Length(&(c->writeBuf)) - c->writeBufPos;
  assert(len > 0);

  size_t wrote;
  const IOStatus writeStatus =
      io_Write(c, buf_Get(&(c->writeBuf)) + c->writeBufPos, len, &wrote);

  switch (writeStatus) {
    case OK:
      io_Verbose(c, "Successfully wrote %zu bytes\n", wrote);
      c->writeBufPos += wrote;
      c->t->writeBytes += wrote;
      if (c->writeBufPos == buf_Length(&(c->writeBuf))) {
        // Whole message body has been written, so stop writing
        ev_io_stop(c->t->loop, &(c->io));
        io_WriteDone(c, 0);
        return 0;
      }
      return 1;
    case FEOF:
      io_Verbose(c, "write wrote zero bytes. Ignoring.\n");
      return 1;
    case SOCKET_ERROR:
    case TLS_ERROR:
      io_Verbose(c, "Error on write");
      ev_io_stop(c->t->loop, &(c->io));
      io_WriteDone(c, -1);
      return -1;
    case NEED_WRITE:
      io_Verbose(c, "I/O would block on writing\n");
      if (c->backwardsIo) {
        io_Verbose(c, "Restoring I/O direction\n");
        c->backwardsIo = 0;
        ev_io_set(&(c->io), c->fd, EV_WRITE);
      }
      return 0;
    case NEED_READ:
      io_Verbose(c, "I/O would block on read while writing\n");
      if (!c->backwardsIo) {
        io_Verbose(c, "Switching I/O direction\n");
        c->backwardsIo = 1;
        ev_io_stop(c->t->loop, &(c->io));
        ev_io_set(&(c->io), c->fd, EV_READ);
        ev_io_start(c->t->loop, &(c->io));
      }
      return 0;
    default:
      assert(0);
      return -1;
  }
}

static void writeReady(struct ev_loop* loop, ev_io* w, int revents) {
  ConnectionState* c = (ConnectionState*)w->data;
  for (int keepOnWriting = 1; keepOnWriting > 0;
       keepOnWriting = singleWrite(c, loop, w, revents))
    ;
}

// Set up libev to asychronously write from writeBuf
void io_SendWrite(ConnectionState* c) {
  ev_init(&(c->io), writeReady);
  ev_io_set(&(c->io), c->fd, EV_WRITE);
  c->backwardsIo = 0;
  c->io.data = c;
  ev_io_start(c->t->loop, &(c->io));
}

static int singleRead(ConnectionState* c, struct ev_loop* loop, ev_io* w,
                      int revents) {
  io_Verbose(c, "I/O ready on read path: %i\n", revents);
  const size_t len = READ_BUF_SIZE - c->readBufPos;
  assert(len > 0);

  size_t readCount;
  const IOStatus readStatus =
      io_Read(c, c->readBuf + c->readBufPos, len, &readCount);

  if (readStatus == OK) {
    io_Verbose(c, "Successfully read %zu bytes\n", readCount);
    c->t->readBytes += readCount;
    // Parse the data we just read plus whatever was left from before
    const size_t parsedLen = readCount + c->readBufPos;

    if (c->t->verbose) {
      fwrite(c->readBuf, parsedLen, 1, stdout);
      printf("\n");
    }

    const size_t parsed = http_parser_execute(&(c->parser), &HttpParserSettings,
                                              c->readBuf, parsedLen);
    io_Verbose(c, "Parsed %zu\n", parsed);
    if (c->parser.http_errno != 0) {
      // Invalid HTTP response. Complete with an error.
      io_Verbose(c, "Parsing error %i\n", c->parser.http_errno);
      ev_io_stop(c->t->loop, &(c->io));
      io_ReadDone(c, -1);
      return -1;
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
      return 0;
    }
    return 1;
  }

  if (readStatus == FEOF) {
    io_Verbose(c, "EOF. Done = %i\n", c->readDone);
    ev_io_stop(c->t->loop, &(c->io));
    io_ReadDone(c, c->readDone ? 0 : -2);
    return 0;
  }

  if ((readStatus == SOCKET_ERROR) || (readStatus == TLS_ERROR)) {
    // Read error. Stop going.
    io_Verbose(c, "Error reading from socket\n");
    ev_io_stop(c->t->loop, &(c->io));
    io_ReadDone(c, -3);
    return -1;
  }

  if (readStatus == NEED_READ) {
    io_Verbose(c, "I/O would block on read\n");
    if (c->backwardsIo) {
      io_Verbose(c, "Restoring I/O direction\n");
      c->backwardsIo = 0;
      ev_io_stop(c->t->loop, &(c->io));
      ev_io_set(&(c->io), c->fd, EV_READ);
      ev_io_start(c->t->loop, &(c->io));
    }
    return 0;
  }

  if (readStatus == NEED_WRITE) {
    io_Verbose(c, "I/O would block on write while reading\n");
    if (!c->backwardsIo) {
      io_Verbose(c, "Switching I/O direction\n");
      c->backwardsIo = 1;
      ev_io_stop(c->t->loop, &(c->io));
      ev_io_set(&(c->io), c->fd, EV_WRITE);
      ev_io_start(c->t->loop, &(c->io));
    }
    return 0;
  }

  assert(0);
  return -1;
}

static void readReady(struct ev_loop* loop, ev_io* w, int revents) {
  ConnectionState* c = (ConnectionState*)w->data;
  for (int keepReading = 1; keepReading > 0;
       keepReading = singleRead(c, loop, w, revents))
    ;
}

// Set up libev to asynchronously read to readBuf
void io_SendRead(ConnectionState* c) {
  ev_init(&(c->io), readReady);
  ev_io_set(&(c->io), c->fd, EV_READ);
  c->io.data = c;
  c->backwardsIo = 0;
  ev_io_start(c->t->loop, &(c->io));
}