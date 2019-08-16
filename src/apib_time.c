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
#include <time.h>

#include "src/apib_time.h"

#define NANOSECOND 1000000000LL
#define NANOSECOND_F 1000000000.0
#define MILLISECOND_F 1000.0

long long apib_GetTime() {
  struct timespec tp;
  int err = clock_gettime(CLOCK_REALTIME, &tp);
  assert(err == 0);
  return (((long long)tp.tv_sec) * NANOSECOND) + tp.tv_nsec;
}

double apib_Seconds(long long t) {
  return ((double)t) / NANOSECOND_F;
}

double apib_Milliseconds(long long t) {
  return ((double)t) / (NANOSECOND_F / MILLISECOND_F);
}