#include <stdio.h>
#include <stdlib.h>

#include <apib_common.h>

#define URL_BUF_LEN 8192
#define INITIAL_URLS 16
#define MAX_ENTROPY_ROUNDS 100
#define SEED_SIZE 8192

static unsigned int urlCount = 0;
static unsigned int urlSize = 0;
static URLInfo*     urls;

const URLInfo* url_GetNext(RandState* rand)
{
  int randVal;

  if (urlCount == 0) {
    return NULL;
  }
  if (urlCount == 1) {
    return &(urls[0]);
  }

#if HAVE_RAND_R
  randVal = rand_r(rand);
#else
  randVal = rand();
#endif

  return &(urls[randVal % urlCount]);
}

static int initUrl(URLInfo* u, apr_pool_t* pool)
{
  apr_status_t s;
  char errBuf[128];

  if (!strcmp(u->url.scheme, "https")) {
    u->isSsl = TRUE;
  } else if (!strcmp(u->url.scheme, "http")) {
    u->isSsl = FALSE;
  } else {
    fprintf(stderr, "Invalid URL scheme\n");
    return -1;
  }

  if (u->url.port_str == NULL) {
    if (u->isSsl) {
      u->port = 443;
    } else {
      u->port = 80;
    }
  } else {
    u->port = atoi(u->url.port_str);
  }

  s = apr_sockaddr_info_get(&(u->address), u->url.hostname,
			    APR_INET, u->port, 0, pool);
  if (s != APR_SUCCESS) {
    apr_strerror(s, errBuf, 128);
    fprintf(stderr, "Error looking up host \"%s\": %s\n", 
	    u->url.hostname, errBuf);
    return -1;
  }

  return 0;
}

int url_InitOne(const char* urlStr, apr_pool_t* pool)
{
  apr_status_t s;

  urlCount = urlSize = 1;
  urls = (URLInfo*)malloc(sizeof(URLInfo));

  s = apr_uri_parse(pool, urlStr, &(urls[0].url));
  if (s != APR_SUCCESS) {
    fprintf(stderr, "Invalid URL\n");
    return -1;
  }

  return initUrl(&(urls[0]), pool);
}

int url_IsSameServer(const URLInfo* u1, const URLInfo* u2)
{
  if (u1->port != u2->port) {
    return 0;
  }
  return apr_sockaddr_equal(u1->address, u2->address);
}

int url_InitFile(const char* fileName, apr_pool_t* pool)
{
  apr_status_t s;
  apr_file_t* file;
  char buf[URL_BUF_LEN];
  LineState line;

  urlCount = 0;
  urlSize = INITIAL_URLS;
  urls = (URLInfo*)malloc(sizeof(URLInfo) * INITIAL_URLS);

  s = apr_file_open(&file, fileName, APR_READ, APR_OS_DEFAULT, pool);
  if (s != APR_SUCCESS) {
    fprintf(stderr, "Can't open \"%s\"\n", fileName);
    return -1;
  }

  linep_Start(&line, buf, URL_BUF_LEN, 0);
  s = linep_ReadFile(&line, file);
  if (s != APR_SUCCESS) {
    apr_file_close(file);
    return -1;
  }

  do {
    while (linep_NextLine(&line)) {
      char* urlStr = linep_GetLine(&line);
      if (urlCount == urlSize) {
	urlSize *= 2;
	urls = (URLInfo*)realloc(urls, sizeof(URLInfo) * urlSize);
      }
      s = apr_uri_parse(pool, urlStr, &(urls[urlCount].url));
      if (s != APR_SUCCESS) {
	fprintf(stderr, "Invalid URL \"%s\"\n", urlStr);
	apr_file_close(file);
	return -1;
      }
      if (initUrl(&(urls[urlCount]), pool) != 0) {
	apr_file_close(file);
	return -1;
      }
      urlCount++;
    }
    linep_Reset(&line);
    s = linep_ReadFile(&line, file);
  } while (s == APR_SUCCESS);

  printf("Read %i URLs from \"%s\"\n", urlCount, fileName);

  apr_file_close(file);
  return 0;
}

void url_InitRandom(RandState* state)
{
  srand(apr_time_now());
}

