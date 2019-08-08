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

#ifndef APIB_COMMON_H
#define APIB_COMMON_H

#include <stdio.h>
#include <config.h>

#include <apr_file_io.h>
#include <apr_network_io.h>
#include <apr_uri.h>
#include <apr_pools.h>
#include <apr_random.h>

/* Random number stuff */
#if HAVE_LRAND48_R
typedef struct drand48_data* RandState;
typedef struct drand48_data RandData;
#elif HAVE_RAND_R
typedef unsigned int* RandState;
typedef unsigned int RandData;
#else
typedef void* RandState;
typedef void* RandData;
#endif

/*
 * Code for managing CPU information.
 */

typedef struct {
  long long idle;
  long long nonIdle;
  apr_time_t timestamp;
} CPUUsage;

/* Return the number of CPUs we have, or 1 if we're unable to determine */
extern int cpu_Count(apr_pool_t* pool);

/* Initialize the CPU library. */
extern void cpu_Init(apr_pool_t* pool);

/* Copy current CPU usage to the CPUUsage object. */
extern void cpu_GetUsage(CPUUsage* usage, apr_pool_t* pool);

/* Get CPU usage data for the interval since we last called this method.
   Result is a ratio (between 0 and 1.0) of CPU used by user + nice + system. 
   Usage is across all CPUs (we counted the CPUs before).
   "usage" must be initialized by cpu_GetUsage the first time. 
   Each call after that to cpu_GetInterval copies the current usage to
   "usage". */
extern double cpu_GetInterval(CPUUsage* usage, apr_pool_t* pool);

/* Return the percent of free RAM */
extern double cpu_GetMemoryUsage(apr_pool_t* pool);


/*
 * Code for handling OAuth
 */


/* Make an "Authorization" header for OAuth 1.0a based on the parameters
 * in "url". The resulting string will be allocated from "pool".
 * If "sendData" is not NULL, it will be used in the signature as well --
 * this should ONLY be set if the content-type is "form-urlencoded."
 * consumer secret must not be null, but tokenSecret may be null in
 * order to implement "one-legged OAuth".
 */
extern char* oauth_MakeAuthorization(const apr_uri_t* url,
				     const char* method,
				     const char* sendData,
				     unsigned int sendDataSize,
				     const char* consumerToken,
				     const char* consumerSecret,
				     const char* accessToken,
				     const char* tokenSecret,
				     apr_pool_t* pool);

/* Same as above, but make an HTTP query string instead. The result
 * will include the entire original query string. */
extern char* oauth_MakeQueryString(const apr_uri_t* url,
				   const char* method,
				   const char* sendData,
				   unsigned int sendDataSize,
				   const char* consumerToken,
				   const char* consumerSecret,
				   const char* accessToken,
				   const char* tokenSecret,
				   apr_pool_t* pool);


#endif
