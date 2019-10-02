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

#include "src/apib_util.h"

#include <cassert>
#include <locale>

namespace apib {

bool eqcase(const std::string& s1, const std::string& s2) {
  if (s1.size() != s2.size()) {
    return false;
  }
  auto i2 = s2.cbegin();
  for (auto i1 = s1.cbegin(); i1 != s1.cend(); i1++) {
    assert(i2 != s2.cend());
    if (tolower(*i1) != tolower(*i2)) {
      return false;
    }
    i2++;
  }
  return true;
}

}  // namespace apib