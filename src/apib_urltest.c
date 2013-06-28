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
#include <stdlib.h>
#include <string.h>

#include <apr_general.h>

#include <apib_common.h>

#define NUM_ITERATIONS 100000
#define NUM_THREADS 8
#define SEED_SIZE 4096

static void* URLThread(apr_thread_t* t, void* arg)
{
  RandState rand = (RandState)malloc(sizeof(RandData));

  url_InitRandom(rand);

  for (int inc = 0; inc < NUM_ITERATIONS; inc++) {
    const URLInfo* url = url_GetNext(rand);
    printf("%s?%s\n", url->url.path, url->url.query);
  }
  return NULL;
}

int main(int argc, char** argv)
{
  apr_pool_t* pool;
  apr_thread_t* threads[NUM_THREADS];

  apr_initialize();
  apr_pool_create(&pool, NULL);
  
  if (argc != 2) {
    fprintf(stderr, "Usage: %s <url file>\n", argv[0]);
    apr_terminate();
    return 2;
  }

  if (url_InitFile(argv[1], pool) != 0) {
    fprintf(stderr, "URL init failed\n");
    apr_terminate();
    return 3;
  }

  for (int i = 0; i < NUM_THREADS; i++) {
    apr_thread_create(&(threads[i]), NULL, URLThread, pool, pool);
  }
  for (int i = 0; i < NUM_THREADS; i++) {
    apr_status_t ret;
    apr_thread_join(&ret, threads[i]);
  }

  apr_pool_destroy(pool);
  apr_terminate();
  return 0;
}
