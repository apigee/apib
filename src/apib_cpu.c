#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <apr_env.h>
#include <apr_file_io.h>
#include <apr_pools.h>
#include <apr_time.h>

#include <apib_common.h>

#define PROC_BUF_LEN 256

static int          CPUCount;
static double       TicksPerSecond;

/* This is a bad way to count the CPUs because "cpuinfo" doesn't account
 * for hyperthreading, etc. */
int cpu_Count(apr_pool_t* pool)
{
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
}

static int getTicks(CPUUsage* cpu, apr_pool_t* pool)
{
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
}

double cpu_GetMemoryUsage(apr_pool_t* pool)
{
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
