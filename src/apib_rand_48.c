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
#include <stdlib.h>
#include <sys/random.h>

#include "apib_rand.h"

typedef struct {
  struct drand48_data data;
} UnixRand;

RandState apib_InitRand() {
  // First, use /dev/urandom -- which is hardware-supported and
  // "random enough" for tests -- to get a seed. This is better
  // than using the time of day or something...
  unsigned short seed[3];
  size_t desiredSeedLen = sizeof(unsigned short) * 3;
  size_t seedLen = getrandom(&seed, desiredSeedLen, 0);
  assert(seedLen == desiredSeedLen);

  UnixRand* r = (UnixRand*)malloc(sizeof(UnixRand));
  seed48_r(seed, &(r->data));
  return r;
}

void apib_FreeRand(RandState s) {
  free(s);
}

long apib_Rand(RandState s) {
  UnixRand* r = (UnixRand*)s;
  long result;
  lrand48_r(&(r->data), &result);
  return result;
}
