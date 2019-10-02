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

#ifndef APIB_UTIL_H
#define APIB_UTIL_H

#include <cstdlib>
#include <iostream>
#include <string>

namespace apib {

#define mandatoryAssert(e)                                           \
  if (!(e)) {                                                        \
    std::cerr << "Assertion failed: " << __FILE__ << ':' << __LINE__ \
              << std::endl;                                          \
    abort();                                                         \
  }

extern bool eqcase(const std::string& s1, const std::string& s2);

}  // namespace apib

#endif  // APIB_UTIL_H
