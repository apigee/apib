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

#ifndef APIB_TEST_SERVER_H
#define APIB_TEST_SERVER_H

#include <pthread.h>

#ifdef __cplusplus
extern "C" {
#endif

#define OP_HELLO 0
#define OP_ECHO 1
#define OP_DATA 2
#define NUM_OPS 3

typedef struct {
  long connectionCount;
  long socketErrorCount;
  long errorCount;
  long successCount;
  long successes[NUM_OPS];
} TestServerStats;

typedef struct {
  int listenfd;
  TestServerStats stats;
  pthread_mutex_t statsLock;
  pthread_t acceptThread;
} TestServer;

/*
Start a simple and dumb thread-per-socket HTTP server on a thread.
*/
extern int testserver_Start(TestServer* s, int port);

/*
Get the port it's listening on.
*/
extern int testserver_GetPort(TestServer* s);

/*
Return statistics about operation.
*/
extern void testserver_GetStats(TestServer* s, TestServerStats* stats);

extern void testserver_ResetStats(TestServer* s);

/*
Stop listening.
*/
extern void testserver_Stop(TestServer* s);

/*
Wait for the listening thread to exit.
*/
extern void testserver_Join(TestServer* s);

#ifdef __cplusplus
}
#endif

#endif  // APIB_TEST_SERVER_H
