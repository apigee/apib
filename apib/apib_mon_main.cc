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

#include <iostream>

#include "apib/apib_mon.h"

int main(int argc, char** argv) {
  apib::MonServer mon;

  if (argc != 2) {
    std::cerr << "Usage: %s " << argv[0] << " <port>" << std::endl;
    return 2;
  }

  int err = mon.start("0.0.0.0", atoi(argv[1]));
  if (err != 0) {
    std::cerr << "Can't start monitoring server: " << err << std::endl;
    return 2;
  }
  std::cout << "apibmon listening on port " << mon.port() << std::endl;

  mon.join();
  return 0;
}
