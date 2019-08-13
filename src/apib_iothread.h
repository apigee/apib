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

#include <pthread.h>

#include "ev.h"
#include "src/apib_rand.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  int             index;
  int             numConnections;
  int             verbose;
  char*           httpVerb;
  char*           sslCipher;
  char*           sendData;
  unsigned int    sendDataSize;
  //SSL_CTX*        sslCtx;
  char**          headers;
  unsigned int    numHeaders;
  unsigned int    thinkTime;
  int             hostHeaderOverride;
  unsigned long*  latencies;
  unsigned int    latenciesSize;
  unsigned int    latenciesCount;
  unsigned long   readCount;
  unsigned long   writeCount;
  unsigned long long readBytes;
  unsigned long long writeBytes;

  // Everything ABOVE must be initialized.
  // Internal stuff -- no need for anyone to set
  pthread_t       thread;
  volatile int    keepRunning;
  RandState       rand;
  struct ev_loop* loop;
} IOThread;

// Start the thread. It's up to the caller to initialize everything
// in the structure above. This call will spawn a thread, and keep
// running until "iothread_Stop" is called.
extern void iothread_Start(IOThread* t);

// Stop the thread. It will block until all I/O operations are complete.
extern void iothread_Stop(IOThread* t);

#ifdef __cplusplus
}
#endif

#endif  // APIB_IOTHREAD_H