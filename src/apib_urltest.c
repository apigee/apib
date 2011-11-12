#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <apr_general.h>

#include <apib_common.h>

#define NUM_ITERATIONS 100000
#define NUM_THREADS 8

static void* URLThread(apr_thread_t* t, void* arg)
{
  URLInfo* url;

  for (int inc = 0; inc < NUM_ITERATIONS; inc++) {
    url = url_GetNext();
    printf("%s?%s\n", url->url.path, url->url.query);
  }
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
    apr_thread_create(&(threads[i]), NULL, URLThread, NULL, pool);
  }
  for (int i = 0; i < NUM_THREADS; i++) {
    apr_status_t ret;
    apr_thread_join(&ret, threads[i]);
  }

  apr_pool_destroy(pool);
  apr_terminate();
  return 0;
}
