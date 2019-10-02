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

#include <unistd.h>

#include "src/apib_cpu.h"

int cpu_Init() { return -1; }

int cpu_Count() { return (int)sysconf(_SC_NPROCESSORS_ONLN); }

void cpu_GetUsage(CPUUsage* usage) {
  usage->idle = 0;
  usage->nonIdle = 0;
  usage->timestamp = 0;
}

double cpu_GetInterval(CPUUsage* usage) { return -1.0; }

double cpu_GetMemoryUsage() { return -1.0; }