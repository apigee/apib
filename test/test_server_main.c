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

#include <stdio.h>
#include <stdlib.h>

#include "test/test_server.h"

int main(int argc, char** argv) {
  if ((argc < 2) || (argc > 4)) {
    fprintf(stderr, "Usage: testserver <port> [<key file> <cert file>]\n");
    return 1;
  }

  int port = atoi(argv[1]);
  char* keyFile = NULL;
  char* certFile = NULL;

  if (argc > 2) {
    keyFile = argv[2];
  }
  if (argc > 3) {
    certFile = argv[3];
  }

  TestServer svr;
  int err = testserver_Start(&svr, port, keyFile, certFile);
  if (err != 0) {
    return 2;
  }

  printf("Listening on port %i\n", testserver_GetPort(&svr));

  testserver_Join(&svr);
  return 0;
}