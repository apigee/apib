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
#include <pthread.h>
#include <netdb.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include "ev.h"
#include "src/apib_iothread.h"
#include "src/apib_rand.h"
#include "src/apib_lines.h"
#include "src/apib_message.h"
#include "src/apib_url.h"

#define READ_BUF_SIZE 128

typedef enum { IDLE, CONNECTED, SENDING, RECEIVING } State;

typedef struct {
  IOThread* t;
  int fd;
  State state;
  ev_io io;
  URLInfo* url;
  StringBuf writeBuf;
  LineState readBuf;
  size_t bufPos;
  HttpMessage* msg;
} ConnectionState;

static void verbose(const IOThread* t, const char* format, ...) {
  if (t->verbose) {
    va_list args;
    va_start(args, format);
    vprintf(format, args);
    va_end(args);
  }
}

static int connectSocket(ConnectionState* c) {
  c->fd = socket(AF_INET, SOCK_STREAM, 0);
  assert(c->fd > 0);

  int yes = 1;
  int err = setsockopt(c->fd, IPPROTO_TCP, TCP_NODELAY, &yes, sizeof(int));
  assert(err == 0);
  err = setsockopt(c->fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int));
  assert(err == 0);

  struct linger linger;
  linger.l_onoff = 0;
  linger.l_linger = 0;
  err = setsockopt(c->fd, SOL_SOCKET, SO_LINGER, &linger, sizeof(struct linger));
  assert(err == 0);

  err = fcntl(c->fd, F_SETFL, O_NONBLOCK);
  assert(err == 0);

  verbose(c->t, "Made new TCP connection\n");

  size_t addrLen;
  const struct sockaddr* addr = url_GetAddress(c->url, c->t->index, &addrLen);
  err = connect(c->fd, addr, addrLen);
  if ((err != 0) && (errno == EINPROGRESS)) {
    return 0;
  }
  return err;
}

void startSend(ConnectionState* c) {
  c->bufPos = 0;
  verbose(c->t, "set pos\n");
  buf_Clear(&(c->writeBuf));
  verbose(c->t, "cleared (len = %i)\n", c->writeBuf.size);
  buf_Append(&(c->writeBuf), c->t->httpVerb);
  buf_Append(&(c->writeBuf), " ");
  buf_Append(&(c->writeBuf), c->url->path);
  buf_Append(&(c->writeBuf), " HTTP/1.1\r\n");
  buf_Append(&(c->writeBuf), "User-Agent: apib\r\n");
  // TODO write other headers, including host override
  buf_Append(&(c->writeBuf), "\r\n");
  verbose(c->t, "wrote\n");
  c->msg = message_NewResponse();
  c->state = SENDING;
}

void sendWrite(ConnectionState* c) {
  // TODO write content too
  char* bp = buf_Get(&(c->writeBuf)) + c->bufPos;
  const size_t len = buf_Length(&(c->writeBuf)) - c->bufPos;
  ssize_t wl = write(c->fd, bp, len);
  verbose(c->t, "Tried to write %i bytes: Wrote %i\n", len, wl);
  if (wl > 0) {
    c->bufPos += wl;
    if (c->bufPos == buf_Length(&(c->writeBuf))) {
      verbose(c->t, "Done writing\n");
      c->state = RECEIVING;
      ev_io_stop(c->t->loop, &(c->io));
      ev_io_set(&(c->io), c->fd, EV_READ);
      ev_io_start(c->t->loop, &(c->io));
    }
  } else if (wl < 0) {
    if ((errno == EAGAIN) || (errno == EWOULDBLOCK)) {
      verbose(c->t, "Would block\n");
    } else {
      verbose(c->t, "Write error: %i\n", errno);
    }
  }
}

int finishMessage(ConnectionState* c, int closeConn) {
  if (c->msg->state != MESSAGE_DONE) {
    fprintf(stderr, "Incomplete HTTP message received\n");
  } else {
    verbose(c->t, "HTTP message complete\n");
  }

  ev_io_stop(c->t->loop, &(c->io));
  message_Free(c->msg);
  linep_Clear(&(c->readBuf));

  if (closeConn) {
    verbose(c->t, "Closing socket\n");
    close(c->fd);
  }

  if (!c->t->keepRunning) {
    verbose(c->t, "Shutting down\n");
    return 1;
  }
  
  if (closeConn) {
    connectSocket(c);
  }
  ev_io_set(&(c->io), c->fd, EV_WRITE);
  c->state = CONNECTED;
  ev_io_start(c->t->loop, &(c->io));
  return 0;
}

void sendRead(ConnectionState* c) {
  int rl = linep_ReadFd(&(c->readBuf), c->fd);
  if (rl == 0) {
    verbose(c->t, "EOF\n");
    finishMessage(c, 1);
  } else if (rl > 0) {
    verbose(c->t, "Read %i bytes\n", rl);
    int err = message_Fill(c->msg, &(c->readBuf));
    if (err != 0) {
      fprintf(stderr, "Fatal error reading message: %i\n", err);
      finishMessage(c, 1);
    } else {
      verbose(c->t, "Message state now %i\n", c->msg->state);
      if (c->msg->state == MESSAGE_DONE) {
        finishMessage(c, 0);
      }
    }
  } else {
    if ((errno == EAGAIN) || (errno == EWOULDBLOCK)) {
      verbose(c->t, "Would block\n");
    } else {
      verbose(c->t, "Read error: %i\n", errno);
    }
  }
}

static void socketReady(struct ev_loop* loop, ev_io* w, int revents) {
  ConnectionState* c = (ConnectionState*)(w->data);
  verbose(c->t, "Socket state %i: (write = %i read = %i)\n", 
    c->state, (revents | EV_READ), (revents | EV_WRITE));

  switch (c->state) {
    case CONNECTED:
      if (revents | EV_WRITE) {
        startSend(c);
      }
      break;
    case SENDING: 
      if (revents | EV_WRITE) {
        sendWrite(c);
      }
      break;
    case RECEIVING:
      if (revents | EV_READ) {
        sendRead(c);
      }
      break;
    default:
      assert(0);
  }
}

static void* ioThread(void* a) {
  IOThread* t = (IOThread*)a;
  ConnectionState* conns = (ConnectionState*)malloc(sizeof(ConnectionState) * t->numConnections);
  t->rand = apib_InitRand();
  verbose(t, "Starting new event loop %i for %i connection\n", t->index, t->numConnections);

  t->loop = ev_loop_new(EVFLAG_AUTO);
  verbose(t, "Backend %i\n", ev_backend(t->loop));

  for (int i = 0; i < t->numConnections; i++) {
    conns[i].t = t;
    conns[i].state = CONNECTED;
    conns[i].url = url_GetNext(t->rand);
    conns[i].bufPos = 0;
  
    buf_New(&(conns[i].writeBuf), READ_BUF_SIZE);
    linep_Start(&(conns[i].readBuf), (char*)malloc(READ_BUF_SIZE),
      READ_BUF_SIZE, 0);
    linep_SetHttpMode(&(conns[i].readBuf), 1);

    int err = connectSocket(&(conns[i]));
    if (err != 0) {
      perror("Error creating non-blocking socket");
      goto finish;
    }

    ev_init(&(conns[i].io), socketReady);
    ev_io_set(&(conns[i].io), conns[i].fd, EV_WRITE);
    conns[i].io.data = &(conns[i]);
    ev_io_start(t->loop, &(conns[i].io));
  }

  int ret = ev_run(t->loop, 0);
  verbose(t, "ev_run finished: %i\n", ret);

finish:
  verbose(t, "Cleaning up event loop %i\n", t->index);
  for (int i = 0; i < t->numConnections; i++) {
    linep_Free(&(conns[i].readBuf));
    buf_Free(&(conns[i].writeBuf));
  }
  free(conns);
  apib_FreeRand(t->rand);
  ev_loop_destroy(t->loop);
  return NULL;
}

void iothread_Start(IOThread* t) {
  t->keepRunning = 1;
  int err = pthread_create(&(t->thread), NULL, ioThread, t);
  assert(err == 0);
}

void iothread_Stop(IOThread* t) {
  t->keepRunning = 0;
  void* ret;
  pthread_join(t->thread, &ret);
}