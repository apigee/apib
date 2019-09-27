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

#ifndef APIB_RAND_H
#define APIB_RAND_H

#include <random>

namespace apib {

/*
This class wraps the very complicated random number stuff in C++.
*/

class RandomGenerator {
 public:
  RandomGenerator();
  int32_t get() { return dist_(engine_); }
  int32_t get(int32_t min, int32_t max);

 private:
  std::minstd_rand engine_;
  std::uniform_int_distribution<int32_t> dist_;
};

}  // namespace apib

#endif  //  APIB_RAND_H
