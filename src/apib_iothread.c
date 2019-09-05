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

#include <assert.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

#include "ev.h"
#include "src/apib_lines.h"
#include "src/apib_rand.h"
#include "src/apib_reporting.h"
#include "src/apib_time.h"
#include "src/apib_url.h"
#include "src/apib_util.h"

static int initialized = 0;
http_parser_settings HttpParserSettings;

static void recycle(ConnectionState* c, int closeConn);

static void freeConnectionState(ConnectionState* c) {
  buf_Free(&(c->writeBuf));
  free(c->readBuf);
  free(c);
}

static int httpComplete(http_parser* p) {
  ConnectionState* c = (ConnectionState*)p->data;
  c->readDone = 1;
  return 0;
}

void writeRequest(ConnectionState* c) {
  // TODO if we didn't change URL, then don't do this every time!
  c->writeBufPos = 0;
  buf_Clear(&(c->writeBuf));
  buf_Append(&(c->writeBuf), c->t->httpVerb);
  buf_Append(&(c->writeBuf), " ");
  buf_Append(&(c->writeBuf), c->url->path);
  buf_Append(&(c->writeBuf), " HTTP/1.1\r\n");
  buf_Append(&(c->writeBuf), "User-Agent: apib\r\n");
  if (!c->t->hostHeaderOverride) {
    buf_Append(&(c->writeBuf), "Host: ");
    buf_Append(&(c->writeBuf), c->url->hostHeader);
    buf_Append(&(c->writeBuf), "\r\n");
  }
  if (c->t->sendDataLen > 0) {
    buf_Append(&(c->writeBuf), "Content-Type: text/plain\r\n");
    buf_Printf(&(c->writeBuf), "Content-Length: %lu\r\n", c->t->sendDataLen);
  }
  if (c->t->oauth != NULL) {
    char* authHdr = oauth_MakeHeader(c->t->randState, c->url, "", c->t->httpVerb,
                                     NULL, 0, c->t->oauth);
    buf_Append(&(c->writeBuf), authHdr);
    buf_Append(&(c->writeBuf), "\r\n");
    free(authHdr);
  }
  if (c->t->noKeepAlive) {
    buf_Append(&(c->writeBuf), "Connection: close\r\n");
  }
  for (int i = 0; i < c->t->numHeaders; i++) {
    buf_Append(&(c->writeBuf), c->t->headers[i]);
    buf_Append(&(c->writeBuf), "\r\n");
  }
  io_Verbose(c, "%s\n", buf_Get(&c->writeBuf));

  buf_Append(&(c->writeBuf), "\r\n");
  if (c->t->sendDataLen > 0) {
    buf_AppendN(&(c->writeBuf), c->t->sendData, c->t->sendDataLen);
  }
  io_Verbose(c, "Total send is %i bytes\n", buf_Length(&(c->writeBuf)));
}

static void connectAndSend(ConnectionState* c) {
  c->startTime = apib_GetTime();
  if (c->needsOpen) {
    int err = io_Connect(c);
    mandatoryAssert(err == 0);
    // Should only fail if we can't create a new socket --
    // errors actually connecting will be handled during write.
    RecordConnectionOpen();
  }
  writeRequest(c);
  io_SendWrite(c);
}

static void thinkingDone(struct ev_loop* loop, ev_timer* t, int revents) {
  assert(revents & EV_TIMER);
  ConnectionState* c = (ConnectionState*)t->data;
  io_Verbose(c, "Think time over\n");
  connectAndSend(c);
}

static void addThinkTime(ConnectionState* c) {
  const double thinkTime = (double)(c->t->thinkTime) / 1000.0;
  io_Verbose(c, "Thinking for %.4lf seconds\n", thinkTime);
  ev_timer_init(&(c->thinkTimer), thinkingDone, thinkTime, 0);
  c->thinkTimer.data = c;
  ev_timer_start(c->t->loop, &(c->thinkTimer));
}

static void recycle(ConnectionState* c, int closeConn) {
  if (closeConn || c->t->noKeepAlive || !c->t->keepRunning) {
    c->needsOpen = 1;
    // Close is async, especially for TLS. So we will
    // reconnect later...
    io_Close(c);
    return;
  }

  c->needsOpen = 0;
  if (c->t->thinkTime > 0) {
    addThinkTime(c);
  } else {
    connectAndSend(c);
  }
}

static int startConnect(ConnectionState* c) {
  c->url = url_GetNext(c->t->randState);
  c->needsOpen = 1;
  connectAndSend(c);
  return 0;
}

void io_CloseDone(ConnectionState* c) {
  if (!c->keepRunning || !c->t->keepRunning) {
    io_Verbose(c, "Connection %i closed and done\n", c->index);
    freeConnectionState(c);
    return;
  }

  if (c->t->thinkTime > 0) {
    addThinkTime(c);
  } else {
    connectAndSend(c);
  }
}

void io_WriteDone(ConnectionState* c, int err) {
  if (err != 0) {
    RecordSocketError();
    io_Verbose(c, "Error on write: %i\n", err);
    recycle(c, 1);
  } else {
    io_Verbose(c, "Write complete. Starting to read\n");
    // Prepare to read.
    // do NOT adjust readBufPos because it may have been left over from another
    // transaction.
    c->readDone = 0;
    http_parser_init(&(c->parser), HTTP_RESPONSE);
    c->parser.data = c;
    io_SendRead(c);
  }
}

void io_ReadDone(ConnectionState* c, int err) {
  if (err != 0) {
    io_Verbose(c, "Error on read: %i\n", err);
    RecordSocketError();
    recycle(c, 1);
    return;
  }

  RecordResult(c->parser.status_code, apib_GetTime() - c->startTime);
  if (!http_should_keep_alive(&(c->parser))) {
    io_Verbose(c, "Server does not want keep-alive\n");
    recycle(c, 1);
  } else {
    const URLInfo* oldUrl = c->url;
    c->url = url_GetNext(c->t->randState);
    if (!url_IsSameServer(oldUrl, c->url, c->t->index)) {
      io_Verbose(c, "Switching to a different server\n");
      recycle(c, 1);
    } else {
      recycle(c, 0);
    }
  }
}

static ConnectionState* initializeConnection(IOThread* t, int index) {
  ConnectionState* c = (ConnectionState*)calloc(1, sizeof(ConnectionState));
  c->index = index;
  c->keepRunning = 1;
  c->t = t;
  buf_New(&(c->writeBuf), WRITE_BUF_SIZE);
  c->readBuf = (char*)malloc(READ_BUF_SIZE);
  return c;
}

static void setNumConnections(IOThread* t, int newVal) {
  iothread_Verbose(t, "Current connections = %i. New connections = %i\n",
                   t->numConnections, newVal);
  if (newVal > t->numConnections) {
    t->connections = (ConnectionState**)realloc(
        t->connections, sizeof(ConnectionState*) * newVal);
    for (int i = t->numConnections; i < newVal; i++) {
      iothread_Verbose(t, "Starting new connection %i\n", i);
      ConnectionState* c = initializeConnection(t, i);
      t->connections[i] = c;
      startConnect(c);
    }

  } else if (newVal < t->numConnections) {
    for (int i = newVal; i < t->numConnections; i++) {
      iothread_Verbose(t, "Nicely asking connection %i to terminate\n", i);
      t->connections[i]->keepRunning = 0;
    }
    t->connections = (ConnectionState**)realloc(
        t->connections, sizeof(ConnectionState*) * newVal);
  }

  t->numConnections = newVal;
}

static void processCommands(struct ev_loop* loop, ev_async* a, int revents) {
  assert(revents & EV_ASYNC);
  IOThread* t = (IOThread*)a->data;
  Command* cmd;
  do {
    cmd = command_Pop(&(t->commands));
    if (cmd != NULL) {
      switch (cmd->command) {
        case STOP:
          t->keepRunning = 0;
          // We added this extra ref before we called ev_run
          ev_unref(t->loop);
          break;
        case SET_CONNECTIONS:
          setNumConnections(t, cmd->newNumConnections);
          break;
        default:
          assert(0);
      }
      free(cmd);
    }
  } while (cmd != NULL);
}

static void* ioThread(void* a) {
  IOThread* t = (IOThread*)a;
  assert(t->numConnections >= 0);
  assert(t->httpVerb != NULL);
  t->readCount = 0;
  t->writeCount = 0;
  t->readBytes = 0;
  t->writeBytes = 0;
  t->randState = apib_InitRand();
  command_Init(&(t->commands));

  iothread_Verbose(t, "Starting new event loop %i for %i connection\n",
                   t->index, t->numConnections);

  t->loop = ev_loop_new(EVFLAG_AUTO);
  iothread_Verbose(t, "libev backend = %i\n", ev_backend(t->loop));
  // Prepare to receive async events, and be sure that we don't block if there
  // are none.
  ev_async_init(&(t->async), processCommands);
  t->async.data = t;
  ev_async_start(t->loop, &(t->async));
  ev_unref(t->loop);

  t->connections =
      (ConnectionState**)malloc(sizeof(ConnectionState*) * t->numConnections);
  for (int i = 0; i < t->numConnections; i++) {
    // First-time initialization of new connection
    ConnectionState* c = initializeConnection(t, i);
    t->connections[i] = c;
    int err = startConnect(c);
    if (err != 0) {
      perror("Error creating non-blocking socket");
      goto finish;
    }
  }

  // Add one more ref count so the loop will stay open even if zero connections
  ev_ref(t->loop);
  int ret = ev_run(t->loop, 0);
  iothread_Verbose(t, "ev_run finished: %i\n", ret);
  RecordByteCounts(t->writeBytes, t->readBytes);

finish:
  iothread_Verbose(t, "Cleaning up event loop %i\n", t->index);
  free(t->connections);
  apib_FreeRand(t->randState);
  ev_loop_destroy(t->loop);
  command_Free(&(t->commands));
  return NULL;
}

void iothread_Start(IOThread* t) {
  if (!initialized) {
    http_parser_settings_init(&HttpParserSettings);
    HttpParserSettings.on_message_complete = httpComplete;
    initialized = 1;
  }
  t->keepRunning = 1;
  int err = pthread_create(&(t->thread), NULL, ioThread, t);
  mandatoryAssert(err == 0);
}

void iothread_Stop(IOThread* t) {
  iothread_Verbose(t, "Signalling to threads to stop running\n");
  Command* cmd = (Command*)malloc(sizeof(Command));
  cmd->command = STOP;
  command_Add(&(t->commands), cmd);
  // Wake up the loop and cause the callback to be called.
  ev_async_send(t->loop, &(t->async));
  void* ret;
  pthread_join(t->thread, &ret);
}

void iothread_SetNumConnections(IOThread* t, int newConnections) {
  Command* cmd = (Command*)malloc(sizeof(Command));
  cmd->command = SET_CONNECTIONS;
  cmd->newNumConnections = newConnections;
  command_Add(&(t->commands), cmd);
  // Wake up the loop and cause the callback to be called.
  ev_async_send(t->loop, &(t->async));
}