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

#include "gtest/gtest.h"
#include "src/apib_cpu.h"

namespace {

TEST(CPU, Memory) {
  double mem = apib::cpu_GetMemoryUsage();
  printf("Memory usage is %lf\n", mem);
  EXPECT_LT(0.0, mem);
  EXPECT_GE(1.0, mem);
}

TEST(CPU, Usage) {
  apib::CPUUsage cpu;

  apib::cpu_GetUsage(&cpu);
  EXPECT_LE(0, cpu.idle);
  EXPECT_LE(0, cpu.nonIdle);

  sleep(1);

  double newUsage = apib::cpu_GetInterval(&cpu);
  printf("Last second: %lf\n", newUsage);
  EXPECT_LT(0.0, newUsage);
  EXPECT_GE(1.0, newUsage);
}

TEST(CPU, MultiUsage) {
  apib::CPUUsage cpu;

  apib::cpu_GetUsage(&cpu);
  EXPECT_LE(0, cpu.idle);
  EXPECT_LE(0, cpu.nonIdle);

  for (int i = 0; i < 10; i++) {
    double newUsage = apib::cpu_GetInterval(&cpu);
    EXPECT_LE(0.0, newUsage);
    EXPECT_GE(1.0, newUsage);
  }
}

}  // namespace

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);

  int err = apib::cpu_Init();
  if (err < 0) {
    printf("Skipping CPU tests: Not supported on this platform.\n");
    return 0;
  }
  printf("The host has %i CPUs\n", apib::cpu_Count());

  return RUN_ALL_TESTS();
}
