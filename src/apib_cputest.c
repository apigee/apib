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
