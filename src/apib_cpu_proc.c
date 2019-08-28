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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/sysctl.h>
#include <sys/times.h>
#include <sys/types.h>
#include <unistd.h>

#include "src/apib_cpu.h"
#include "src/apib_lines.h"
#include "src/apib_time.h"

#define PROC_BUF_LEN 8192

static int CPUCount;
static double TicksPerSecond;

/* Count the CPUs in a simple way by counting "processor" lines in /proc/cpuinfo
 */
int cpu_Count() {
  FILE* cpuinfo = fopen("/proc/cpuinfo", "r");
  if (cpuinfo == NULL) {
    return 1;
  }

  LineState line;
  char buf[PROC_BUF_LEN];
  linep_Start(&line, buf, PROC_BUF_LEN, 0);

  int count = 0;
  int rc;

  do {
    rc = linep_ReadFile(&line, cpuinfo);
    if (rc > 0) {
      while (linep_NextLine(&line)) {
        char* l = linep_GetLine(&line);
        if (!strncmp(l, "processor ", 10) || !strncmp(l, "processor\t", 10)) {
          count++;
        }
      }
      linep_Reset(&line);
    }
  } while (rc > 0);
  fclose(cpuinfo);

  if (count < 1) {
    count = 1;
  }
  return count;
}

int cpu_Init() {
  char* countStr;

  TicksPerSecond = sysconf(_SC_CLK_TCK);

  countStr = getenv("CPU_COUNT");
  if (countStr != NULL) {
    CPUCount = atoi(countStr);
    return 0;
  }

  CPUCount = cpu_Count();
  if (CPUCount < 0) {
    return -1;
  }
  return 0;
}

double cpu_GetMemoryUsage() {
  FILE* meminfo = fopen("/proc/meminfo", "r");
  if (meminfo == NULL) {
    return 0.0;
  }

  // PROC_BUF_LEN should be big enough to hold all of /proc/meminfo!
  char buf[PROC_BUF_LEN];
  LineState line;
  linep_Start(&line, buf, PROC_BUF_LEN, 0);
  int rc = linep_ReadFile(&line, meminfo);
  fclose(meminfo);
  if (rc < 0) {
    return 0.0;
  }

  long totalMem = 0;
  long freeMem = 0;
  long buffers = 0;
  long cache = 0;

  while (linep_NextLine(&line)) {
    char* n = linep_NextToken(&line, " ");
    char* v = linep_NextToken(&line, " ");

    if (!strcmp(n, "MemTotal:")) {
      totalMem = atol(v);
    } else if (!strcmp(n, "MemFree:")) {
      freeMem = atol(v);
    } else if (!strcmp(n, "Buffers:")) {
      buffers = atol(v);
    } else if (!strcmp(n, "Cached:")) {
      cache = atol(v);
    }
  }

  if ((totalMem <= 0) || (freeMem <= 0)) {
    return 0.0;
  }

  return (double)(totalMem - (freeMem + buffers + cache)) / (double)totalMem;
}

#ifdef __FreeBSD__

static int getTicks(CPUUsage* cpu, apr_pool_t* pool) {
  struct tms ticks;
  cpu->idle = times(&ticks);
  if (cpu->idle == -1) return 0;
  cpu->nonIdle = ticks.tms_utime + ticks.tms_stime;
  return 1;
}

#else

static int getTicks(CPUUsage* cpu) {
  FILE* stat = fopen("/proc/stat", "r");
  if (stat == NULL) {
    return 0;
  }

  char buf[PROC_BUF_LEN];
  LineState line;

  linep_Start(&line, buf, PROC_BUF_LEN, 0);
  const int rc = linep_ReadFile(&line, stat);
  fclose(stat);
  if (rc < 0) {
    return 0;
  }

  while (linep_NextLine(&line)) {
    char* l = linep_GetLine(&line);
    if (!strncmp(l, "cpu ", 4)) {
      // Read the "cpu" line, which is a sum of all CPUs
      long long idleCount = 0LL;
      long long nonIdleCount = 0LL;
      int i = 0;
      char* tok;

      linep_NextToken(&line, " \t");
      do {
        tok = linep_NextToken(&line, " \t");
        if (tok != NULL) {
          if ((i == 3) || (i == 4) || (i == 7)) {
            /* The fourth and fifth columns are "idle" and "iowait".
                 We consider both to be idle CPU.
               The eigth is "steal", which is time lost to virtualization
               as a client -- that's idle two in our estimation */
            idleCount += atoll(tok);
          } else {
            nonIdleCount += atoll(tok);
          }
          i++;
        }
      } while (tok != NULL);
      cpu->nonIdle = nonIdleCount;
      cpu->idle = idleCount;
      return 1;
    }
  }

  return 0;
}

#endif

#ifdef __FreeBSD__

double cpu_GetMemoryUsage(apr_pool_t* pool) {
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

#else

#endif

void cpu_GetUsage(CPUUsage* cpu) {
  getTicks(cpu);
  cpu->timestamp = apib_GetTime();
}

double cpu_GetInterval(CPUUsage* oldCpu) {
  CPUUsage cpu;

  if (!getTicks(&cpu)) {
    return 0;
  }
  cpu.timestamp = apib_GetTime();

  const long long idleTicks = cpu.idle - oldCpu->idle;
  const long long usageTicks = cpu.nonIdle - oldCpu->nonIdle;

  memcpy(oldCpu, &cpu, sizeof(CPUUsage));

  return ((double)usageTicks / (double)(idleTicks + usageTicks));
}
