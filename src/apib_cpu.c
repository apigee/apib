/*
   Copyright 2013 Apigee Corp.

   Licensed under the Apache License, Version 2.0 (the "License");
   you may not use this file except in compliance with the License.
   You may obtain a copy of the License at

       http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software
   distributed under the License is distributed on an "AS IS" BASIS,
   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
   See the License for the specific language governing permissions and
   limitations under the License.
*/

#ifdef __FreeBSD__
#include <sys/types.h>
#include <sys/sysctl.h>
#include <sys/times.h>
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <apr_env.h>
#include <apr_file_io.h>
#include <apr_pools.h>
#include <apr_time.h>

#include <apib_common.h>

#define PROC_BUF_LEN 8192

static int          CPUCount;
static double       TicksPerSecond;

/* This is a bad way to count the CPUs because "cpuinfo" doesn't account
 * for hyperthreading, etc. */
int cpu_Count(apr_pool_t* pool)
{
#ifdef _SC_NPROCESSORS_ONLN
  return (int)sysconf(_SC_NPROCESSORS_ONLN);
#else
  apr_status_t s;
  apr_file_t* f;
  char buf[PROC_BUF_LEN];
  LineState line;
  int count = 0;
  
  s = apr_file_open(&f, "/proc/cpuinfo", APR_READ, APR_OS_DEFAULT, pool);
  if (s != APR_SUCCESS) {
    return 1;
  }

  linep_Start(&line, buf, PROC_BUF_LEN, 0);
  do {
    s = linep_ReadFile(&line, f);
    if (s == APR_SUCCESS) {
      while (linep_NextLine(&line)) {
        char* l = linep_GetLine(&line);
	if (!strncmp(l, "processor ", 10) ||
	    !strncmp(l, "processor\t", 10)) {
	  count++;
	}
      }
      linep_Reset(&line);
    }
  } while (s == APR_SUCCESS);
  apr_file_close(f);

  if (count < 1) {
    count = 1;
  }
  return count;
#endif
}

static int getTicks(CPUUsage* cpu, apr_pool_t* pool)
{
#ifdef __FreeBSD__
  struct tms ticks;
  cpu->idle = times(&ticks);
  if (cpu->idle == -1)
    return 0;
  cpu->nonIdle = ticks.tms_utime + ticks.tms_stime;
  return 1;
#else
  apr_status_t s;
  apr_file_t* proc;
  char buf[PROC_BUF_LEN];
  LineState line;
  
  s = apr_file_open(&proc, "/proc/stat", APR_READ, APR_OS_DEFAULT, pool);
  if (s != APR_SUCCESS) {
    return 0;
  }

  linep_Start(&line, buf, PROC_BUF_LEN, 0);
  s = linep_ReadFile(&line, proc);
  apr_file_close(proc);
  if (s != APR_SUCCESS) {
    return 0;
  }

  while (linep_NextLine(&line)) {
    char* l = linep_GetLine(&line);
    if (!strncmp(l, "cpu ", 4)) {
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
#endif
}

double cpu_GetMemoryUsage(apr_pool_t* pool)
{
#ifdef __linux__
  apr_status_t s;
  apr_file_t* proc;
  char buf[PROC_BUF_LEN];
  LineState line;
  
  s = apr_file_open(&proc, "/proc/meminfo", APR_READ, APR_OS_DEFAULT, pool);
  if (s != APR_SUCCESS) {
    return 0.0;
  }

  linep_Start(&line, buf, PROC_BUF_LEN, 0);
  s = linep_ReadFile(&line, proc);
  apr_file_close(proc);
  if (s != APR_SUCCESS) {
    return 0.0;
  }
#endif

  long totalMem = 0;
  long freeMem = 0;
  long buffers = 0;
  long cache = 0;

#ifdef __FreeBSD__
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
#else
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
#endif

  if ((totalMem <= 0) || (freeMem <= 0)) {
    return 0.0;
  }

  return (double)(totalMem - (freeMem + buffers + cache)) / (double)totalMem;
}

void cpu_Init(apr_pool_t* pool)
{
  char* countStr;

  TicksPerSecond = sysconf(_SC_CLK_TCK);

  if (apr_env_get(&countStr, "CPU_COUNT", pool) == APR_SUCCESS) {
    CPUCount = atoi(countStr);
  } else {
    CPUCount = cpu_Count(pool);
  }
}

void cpu_GetUsage(CPUUsage* cpu, apr_pool_t* pool)
{
  getTicks(cpu, pool);
  cpu->timestamp = apr_time_now();
}

double cpu_GetInterval(CPUUsage* oldCpu, apr_pool_t* pool)
{
  CPUUsage cpu;
  long long usageTicks;
  long long idleTicks;

  if (!getTicks(&cpu, pool)) {
    return 0;
  }
  cpu.timestamp = apr_time_now();

  idleTicks = cpu.idle - oldCpu->idle;
  usageTicks = cpu.nonIdle - oldCpu->nonIdle;

  memcpy(oldCpu, &cpu, sizeof(CPUUsage));

  return ((double)usageTicks / (double)(idleTicks + usageTicks));
}
