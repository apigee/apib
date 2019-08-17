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

static int initialized = 0;
http_parser_settings HttpParserSettings;

static void printVerbose(const char* format, va_list args) {
  vprintf(format, args);
}

void io_Verbose(ConnectionState* c, const char* format, ...) {
  if (c->t->verbose) {
    va_list args;
    va_start(args, format);
    printVerbose(format, args);
    va_end(args);
  }
}

void verbose(IOThread* t, const char* format, ...) {
  if (t->verbose) {
    va_list args;
    va_start(args, format);
    printVerbose(format, args);
    va_end(args);
  }
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
  if (c->t->sendDataLen > 0) {
    buf_Append(&(c->writeBuf), "Content-Type: text-plain\r\n");
    buf_Printf(&(c->writeBuf), "Content-Length: %lu\r\n", c->t->sendDataLen);
  }
  // TODO write other headers, including host override
  buf_Append(&(c->writeBuf), "\r\n");
  if (c->t->sendDataLen > 0) {
    buf_AppendN(&(c->writeBuf), c->t->sendData, c->t->sendDataLen);
  }
  c->state = SENDING;
}

static void recycle(ConnectionState* c, int closeConn) {
  if (closeConn) {
    io_Close(c);
    c->startTime = apib_GetTime();
    int err = io_Connect(c);
    // Should only fail if we can't create a new socket --
    // errors actually connecting will be handled during write.
    assert(err == 0);
    RecordConnectionOpen();
  }
  writeRequest(c);
  io_SendWrite(c);
}

static int startConnect(ConnectionState* c) {
  c->url = url_GetNext(c->t->rand);
  if (c->url->isSsl) {
    // TODO
    assert(0);
  }

  c->startTime = apib_GetTime();
  const int err = io_Connect(c);
  if (err == 0) {
    RecordConnectionOpen();
    writeRequest(c);
    io_SendWrite(c);
  }
  return err;
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
    if (c->url->isSsl) {
      // TODO
      assert(0);
    }
    io_SendRead(c);
  }
}

void io_ReadDone(ConnectionState* c, int err) {
  if (err != 0) {
    io_Verbose(c, "Error on read: %i\n", err);
    RecordSocketError();
    if (c->t->keepRunning) {
      recycle(c, 1);
    } else {
      io_Verbose(c, "Stopping\n");
      io_Close(c);
    }
    return;
  }

  RecordResult(c->parser.status_code, apib_GetTime() - c->startTime);
  if (!c->t->keepRunning) {
    io_Verbose(c, "Stopping\n");
    io_Close(c);
    return;
  }

  if (!http_should_keep_alive(&(c->parser))) {
    io_Verbose(c, "Server does not want keep-alive\n");
    recycle(c, 1);
  } else {
    const URLInfo* oldUrl = c->url;
    c->url = url_GetNext(c->t->rand);
    if (!url_IsSameServer(oldUrl, c->url, c->t->index)) {
      io_Verbose(c, "Switching to a different server\n");
      recycle(c, 1);
    } else {
      recycle(c, 0);
    }
  }
}

static void* ioThread(void* a) {
  IOThread* t = (IOThread*)a;
  // TODO increment all these while running
  t->latenciesSize = 1024;
  t->latenciesCount = 0;
  t->latencies = (long long*)malloc(sizeof(long long) * 1024);
  t->readCount = 0;
  t->writeCount = 0;
  t->readBytes = 0;
  t->writeBytes = 0;

  ConnectionState* conns =
      (ConnectionState*)malloc(sizeof(ConnectionState) * t->numConnections);
  t->rand = apib_InitRand();
  verbose(t, "Starting new event loop %i for %i connection\n", t->index,
          t->numConnections);

  t->loop = ev_loop_new(EVFLAG_AUTO);
  verbose(t, "Backend %i\n", ev_backend(t->loop));

  for (int i = 0; i < t->numConnections; i++) {
    // First-time initialization of new connection
    ConnectionState* s = &(conns[i]);
    s->t = t;
    s->state = IDLE;
    conns[i].url = NULL;
    buf_New(&(conns[i].writeBuf), WRITE_BUF_SIZE);
    s->writeBufPos = 0;
    s->readBuf = (char*)malloc(READ_BUF_SIZE);
    s->readBufPos = 0;

    int err = startConnect(&(conns[i]));
    if (err != 0) {
      perror("Error creating non-blocking socket");
      goto finish;
    }
  }

  int ret = ev_run(t->loop, 0);
  verbose(t, "ev_run finished: %i\n", ret);
  RecordByteCounts(t->writeBytes, t->readBytes);

finish:
  verbose(t, "Cleaning up event loop %i\n", t->index);
  for (int i = 0; i < t->numConnections; i++) {
    ConnectionState* s = &(conns[i]);
    buf_Free(&(s->writeBuf));
    free(s->readBuf);
  }
  free(conns);
  free(t->latencies);
  apib_FreeRand(t->rand);
  ev_loop_destroy(t->loop);
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
  assert(err == 0);
}

void iothread_Stop(IOThread* t) {
  t->keepRunning = 0;
  void* ret;
  pthread_join(t->thread, &ret);
}