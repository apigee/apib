#include <stdio.h>
#include <string.h>

#include <apr_time.h>

#include <apib_common.h>

int main(int argc, char** argv)
{
  apr_pool_t* pool;
  CPUUsage cpu;

  apr_initialize();
  apr_pool_create(&pool, NULL);

  cpu_Init(pool);

  printf("We think that we have %i CPUs\n", cpu_Count(pool));

  cpu_GetUsage(&cpu, pool);
  apr_sleep(apr_time_from_sec(5));
  printf("%.2lf\n", cpu_GetInterval(&cpu, pool));

  apr_pool_destroy(pool);
  apr_terminate();
 
  return 0;
}
