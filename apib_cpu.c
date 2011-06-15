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

static void setLL(LineState* line, long long* val) 
{
  char* tok = linep_NextToken(line, " \t");
  if (tok != NULL) {
    *val = atoll(tok);
  }
}

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
      linep_NextToken(&line, " \t");
      setLL(&line, &(cpu->user));
      setLL(&line, &(cpu->nice));
      setLL(&line, &(cpu->system));
      setLL(&line, &(cpu->idle));
      setLL(&line, &(cpu->ioWait));
      return 1;
    }
  }

  return 0;
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
  double elapsedTicks;
  long long usageTicks;

  if (!getTicks(&cpu, pool)) {
    return 0;
  }
  cpu.timestamp = apr_time_now();

  /* apr_time_t is in microseconds */
  elapsedTicks = (cpu.timestamp - oldCpu->timestamp) / 1000000.0 * 
                 TicksPerSecond * CPUCount;

  /*
  usageTicks = 
    (cpu.user - oldCpu->user) +
    (cpu.nice - oldCpu->nice) +
    (cpu.system - oldCpu->system);
  */
  usageTicks = cpu.idle - oldCpu->idle;
  
  memcpy(oldCpu, &cpu, sizeof(CPUUsage));

  return 1.0 - (usageTicks / elapsedTicks);
}
