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

#include <stdarg.h>
#include <string.h>

int safeSprintf(char* str, size_t size, const char* format, ...) {
  va_list ap;
  va_start(ap, format);
  const int ret = vsnprintf(str, size, format, ap);
  va_end(ap);
  if (ret >= size) {
    str[size - 1] = 0;
  }
  return ret;
}
