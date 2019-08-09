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

typedef struct {
  int listenfd;
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
