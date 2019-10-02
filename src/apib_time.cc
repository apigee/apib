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

#include <cassert>
#include <ctime>

#include "src/apib_time.h"
#include "src/apib_util.h"

namespace apib {

static const int64_t kNanosecond = 1000000000LL;
static const double kNanosecondF = 1000000000.0;
static const double kMillisecondF = 1000.0;

int64_t GetTime() {
  struct timespec tp;
  const int err = clock_gettime(CLOCK_REALTIME, &tp);
  mandatoryAssert(err == 0);
  return (((int64_t)tp.tv_sec) * kNanosecond) + tp.tv_nsec;
}

double Seconds(int64_t t) { return ((double)t) / kNanosecondF; }

double Milliseconds(int64_t t) {
  return ((double)t) / (kNanosecondF / kMillisecondF);
}

}  // namespace apib