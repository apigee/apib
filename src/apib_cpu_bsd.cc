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

/* Don't let clang-format sort includes, or this will not compile. */
#include <sys/types.h>
#include <sys/sysctl.h>
#include <sys/times.h>
#include <unistd.h>

#include <cstring>

#include "src/apib_cpu.h"
#include "src/apib_time.h"

namespace apib {

int cpu_Init() { return 0; }

int cpu_Count() { return (int)sysconf(_SC_NPROCESSORS_ONLN); }

static int getTicks(CPUUsage* cpu) {
  struct tms ticks;
  cpu->idle = times(&ticks);
  if (cpu->idle == -1) return 0;
  cpu->nonIdle = ticks.tms_utime + ticks.tms_stime;
  return 1;
}

double cpu_GetMemoryUsage() {
  long totalMem = 0;
  long freeMem = 0;
  long buffers = 0;
  long cache = 0;

  /* Let's work with kilobytes. */
  long pagesize = sysconf(_SC_PAGESIZE) / 1024;
  totalMem = sysconf(_SC_PHYS_PAGES) * pagesize;

  size_t len;

  unsigned free;
  len = sizeof(free);
  sysctlbyname("vm.stats.vm.v_free_count", &free, &len, NULL, 0);
  freeMem = free * pagesize;

  /* `buffers' is of expected type (long), no need for another variable. */
  len = sizeof(buffers);
  sysctlbyname("vfs.bufspace", &buffers, &len, NULL, 0);
  buffers /= 1024;

  /* `cache' is number of inactive pages since r309017. */
  unsigned inact;
  len = sizeof(inact);
  sysctlbyname("vm.stats.vm.v_inactive_count", &inact, &len, NULL, 0);
  cache = inact * pagesize;

  if ((totalMem <= 0) || (freeMem <= 0)) {
    return 0.0;
  }

  return (double)(totalMem - (freeMem + buffers + cache)) / (double)totalMem;
}

void cpu_GetUsage(CPUUsage* cpu) {
  getTicks(cpu);
  cpu->timestamp = GetTime();
}

double cpu_GetInterval(CPUUsage* oldCpu) {
  CPUUsage cpu;

  if (!getTicks(&cpu)) {
    return 0;
  }
  cpu.timestamp = GetTime();

  const long long idleTicks = cpu.idle - oldCpu->idle;
  const long long usageTicks = cpu.nonIdle - oldCpu->nonIdle;
  const long long allUsageTicks = idleTicks + usageTicks;

  memcpy(oldCpu, &cpu, sizeof(CPUUsage));

  if (allUsageTicks == 0) {
    return 0.0;
  }
  return ((double)usageTicks / (double)allUsageTicks);
}

}  // namespace apib
