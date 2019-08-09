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
#include <sys/time.h>

#include "apib_rand.h"

typedef struct {
  unsigned int seed;
} Rand;

RandState apib_InitRand() {
  // Seed the generator in the simplest possible way, since other more
  // complex ways aren't available on this platform.
  struct timeval tv;
  Rand* r = (Rand*)malloc(sizeof(Rand));
  gettimeofday(&tv, NULL);
  r->seed = tv.tv_sec;
  return r;
}

void apib_FreeRand(RandState s) {
  free(s);
}

long apib_Rand(RandState s) {
  Rand* r = (Rand*)s;
  return rand_r(&(r->seed));
}
