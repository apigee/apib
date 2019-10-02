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

#include "src/apib_cpu.h"

#include <cstring>
#include <fstream>

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include "src/apib_lines.h"
#include "src/apib_time.h"

#define PROC_BUF_LEN 8192

namespace apib {

static int CPUCount;
static double TicksPerSecond;

/* Count the CPUs in a simple way by counting "processor" lines in /proc/cpuinfo
 */
int cpu_Count() {
  std::ifstream in("/proc/cpuinfo");
  if (in.fail()) {
    return 1;
  }

  LineState line(PROC_BUF_LEN);
  int count = 0;
  int rc;

  do {
    rc = line.readStream(in);
    if (rc > 0) {
      while (line.next()) {
        const auto l = line.line();
        const auto ten = l.substr(0, 10);
        if (("processor " == ten) || ("processor\t" == ten)) {
          count++;
        }
      }
      line.consume();
    }
  } while (rc > 0);
  in.close();

  if (count < 1) {
    count = 1;
  }
  return count;
}

int cpu_Init() {
  struct stat statBuf;

  int err = stat("/proc/stat", &statBuf);
  if (err != 0) {
    return -1;
  }
  err = stat("/proc/meminfo", &statBuf);
  if (err != 0) {
    return -2;
  }

  TicksPerSecond = sysconf(_SC_CLK_TCK);

  const char* countStr = getenv("CPU_COUNT");
  if (countStr != NULL) {
    CPUCount = atoi(countStr);
    return 0;
  }

  CPUCount = cpu_Count();
  if (CPUCount < 0) {
    return -3;
  }
  return 0;
}

double cpu_GetMemoryUsage() {
  std::ifstream meminfo("/proc/meminfo");
  if (meminfo.fail()) {
    return 0.0;
  }

  // PROC_BUF_LEN should be big enough to hold all of /proc/meminfo!
  LineState line(PROC_BUF_LEN);

  int rc = line.readStream(meminfo);
  if (rc < 0) {
    return 0.0;
  }

  long totalMem = 0;
  long freeMem = 0;
  long buffers = 0;
  long cache = 0;

  while (line.next()) {
    const auto n = line.nextToken(" ");
    const auto v = line.nextToken(" ");

    if ("MemTotal:" == n) {
      totalMem = std::stol(v);
    } else if ("MemFree:" == n) {
      freeMem = std::stol(v);
    } else if ("Buffers:" == n) {
      buffers = std::stol(v);
    } else if ("Cached:" == n) {
      cache = std::stol(v);
    }
  }

  if ((totalMem <= 0) || (freeMem <= 0)) {
    return 0.0;
  }

  return (double)(totalMem - (freeMem + buffers + cache)) / (double)totalMem;
}

static int getTicks(CPUUsage* cpu) {
  std::ifstream stat("/proc/stat");
  if (stat.fail()) {
    return 0;
  }

  LineState line(PROC_BUF_LEN);

  const int rc = line.readStream(stat);
  if (rc < 0) {
    return 0;
  }

  while (line.next()) {
    const auto l = line.line();
    if ("cpu " == l.substr(0, 4)) {
      // Read the "cpu" line, which is a sum of all CPUs
      int64_t idleCount = 0LL;
      int64_t nonIdleCount = 0LL;
      int i = 0;
      std::string tok;

      line.nextToken(" \t");
      do {
        tok = line.nextToken(" \t");
        if (!tok.empty()) {
          if ((i == 3) || (i == 4) || (i == 7)) {
            /* The fourth and fifth columns are "idle" and "iowait".
                 We consider both to be idle CPU.
               The eigth is "steal", which is time lost to virtualization
               as a client -- that's idle two in our estimation */
            idleCount += std::stoll(tok);
          } else {
            nonIdleCount += std::stoll(tok);
          }
          i++;
        }
      } while (!tok.empty());
      cpu->nonIdle = nonIdleCount;
      cpu->idle = idleCount;
      return 1;
    }
  }

  return 0;
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

}  // namespace