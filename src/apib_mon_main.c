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

#include "src/apib_mon.h"

int main(int argc, char** argv) {
  MonServer mon;

  if (argc != 2) {
    fprintf(stderr, "Usage: %s <port>\n", argv[0]);
    return 2;
  }

  int err = mon_StartServer(&mon, "0.0.0.0", atoi(argv[1]));
  if (err != 0) {
    fprintf(stderr, "Can't start monitoring server: %i\n", err);
    return 2;
  }
  printf("apibmon listening on port %i\n", mon_GetPort(&mon));

  mon_JoinServer(&mon);
  return 0;
}
