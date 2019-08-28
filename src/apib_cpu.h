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

#ifndef APIB_CPU_H
#define APIB_CPU_H

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Code for managing CPU information.
 */
typedef struct {
  long long idle;
  long long nonIdle;
  long long timestamp;
} CPUUsage;

/* Return the number of CPUs we have, or 1 if we're unable to determine */
extern int cpu_Count();

/* Initialize the CPU library. Returns 0 if we can actually measure
   CPU usage on this platform. */
extern int cpu_Init();

/* Copy current CPU usage to the CPUUsage object. */
extern void cpu_GetUsage(CPUUsage* usage);

/* Get CPU usage data for the interval since we last called this method.
   Result is a ratio (between 0 and 1.0) of CPU used by user + nice + system.
   Usage is across all CPUs (we counted the CPUs before).
   "usage" must be initialized by cpu_GetUsage the first time.
   Each call after that to cpu_GetInterval copies the current usage to
   "usage". */
extern double cpu_GetInterval(CPUUsage* usage);

/* Return the percent of free RAM, or a negative number if we don't know */
extern double cpu_GetMemoryUsage();

#ifdef __cplusplus
}
#endif

#endif  // APIB_CPU_H