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

#include "apib_url.h"

#include <assert.h>
#include <netdb.h>
#include <regex.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>

#include "src/apib_lines.h"

#define URL_BUF_LEN 8192
#define INITIAL_URLS 16

#define URL_REGEXP "^(https?):\\/\\/([a-zA-Z0-9\\-\\.]+)(:([0-9]+))?(\\/.*)?$"
// Number of "()"s in the expression above, plus 1
#define URL_REGEXP_MATCHES 6

static unsigned int urlCount = 0;
static unsigned int urlSize = 0;
static URLInfo* urls;
static int initialized = 0;

static regex_t urlExpression;

static void initializeExpression() {
  const int e = regcomp(&urlExpression, URL_REGEXP, REG_EXTENDED);
  assert(e == 0);
}

static int initHost(const char* hostname, URLInfo* u) {
  struct addrinfo hints;
  struct addrinfo* results;

  // For now, look up only IP V4 addresses
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_protocol = 0;
  hints.ai_flags = 0;

  const int addrerr = getaddrinfo(hostname, NULL, &hints, &results);
  if (addrerr) {
    return -1;
  }

  struct addrinfo* a = results;
  u->addresses = results;
  int c = 1;
  for (; a->ai_next != NULL; c++) {
    a = a->ai_next;
  }
  u->addressCount = c;
  return 0;
}

static char* getRegexPart(const char* urlstr, const regmatch_t* matches,
                          const int ix) {
  const regmatch_t* match = &(matches[ix]);
  if (match->rm_so >= 0) {
    assert(match->rm_eo >= match->rm_so);
    return strndup(urlstr + match->rm_so, match->rm_eo - match->rm_so);
  }
  return NULL;
}

static int initUrl(const char* urlstr, URLInfo* u) {
  regmatch_t matches[URL_REGEXP_MATCHES];
  const int match =
      regexec(&urlExpression, urlstr, URL_REGEXP_MATCHES, matches, 0);
  if (match) {
    char errBuf[128];
    regerror(match, &urlExpression, errBuf, 128);
    fprintf(stderr, "Error matching URL: %s\n", errBuf);
    return -1;
  }

  // Match 1 is either "http" or "https". Just count characters.
  u->isSsl = ((matches[1].rm_eo - matches[1].rm_so) == 5);

  // Match 2 is the "hostname"
  char* hoststr = getRegexPart(urlstr, matches, 2);
  assert(hoststr != NULL);
  const int hosterr = initHost(hoststr, u);
  free(hoststr);
  if (hosterr) {
    // No addresses, which is OK now
    u->addresses = NULL;
    u->addressCount = 0;
  }

  // Match 4 is the port number, if any
  char* portstr = getRegexPart(urlstr, matches, 4);
  if (portstr != NULL) {
    u->port = atoi(portstr);
    free(portstr);
  } else if (u->isSsl) {
    u->port = 443;
  } else {
    u->port = 80;
  }

  // Match 5 is the path, if any
  char* path = getRegexPart(urlstr, matches, 5);
  if (path != NULL) {
    u->path = path;
  } else {
    // strdup here because "Reset" will actually call "free"
    u->path = strdup("/");
  }

  return 0;
}

/*
static apr_sockaddr_t* getConn(const URLInfo* u, int index) {
  return u->addresses[index % u->addressCount];
}
*/

int url_InitOne(const char* urlStr) {
  assert(!initialized);
  initializeExpression();

  urlCount = urlSize = 1;
  urls = (URLInfo*)malloc(sizeof(URLInfo));
  const int e = initUrl(urlStr, &(urls[0]));
  if (e == 0) {
    initialized = 1;
  }
  return e;
}

/*
apr_sockaddr_t* url_GetAddress(const URLInfo* url, int index) {
  apr_sockaddr_t* a = getConn(url, index);

#if DEBUG
  char* str;
  apr_sockaddr_ip_get(&str, a);
  printf("Connecting to %s\n", str);
#endif
  return a;
}

int url_IsSameServer(const URLInfo* u1, const URLInfo* u2, int index) {
  if (u1->port != u2->port) {
    return 0;
  }
  return apr_sockaddr_equal(getConn(u1, index), getConn(u2, index));
}
*/

int url_InitFile(const char* fileName) {
  assert(!initialized);
  initializeExpression();

  FILE* file;
  char buf[URL_BUF_LEN];
  LineState line;

  urlCount = 0;
  urlSize = INITIAL_URLS;
  urls = (URLInfo*)malloc(sizeof(URLInfo) * INITIAL_URLS);

  file = fopen(fileName, "r");
  if (file == NULL) {
    fprintf(stderr, "Can't open \"%s\"\n", fileName);
    return -1;
  }

  linep_Start(&line, buf, URL_BUF_LEN, 0);
  int rc = linep_ReadFile(&line, file);
  if (rc < 0) {
    fclose(file);
    return -1;
  }

  do {
    while (linep_NextLine(&line)) {
      char* urlStr = linep_GetLine(&line);
      if (urlCount == urlSize) {
        urlSize *= 2;
        urls = (URLInfo*)realloc(urls, sizeof(URLInfo) * urlSize);
      }

      int err = initUrl(urlStr, &(urls[urlCount]));
      if (err) {
        fprintf(stderr, "Invalid URL \"%s\"\n", urlStr);
        fclose(file);
        return -1;
      }
      urlCount++;
    }
    linep_Reset(&line);
    rc = linep_ReadFile(&line, file);
  } while (rc > 0);

  printf("Read %i URLs from \"%s\"\n", urlCount, fileName);

  fclose(file);
  initialized = 1;
  return 0;
}

const URLInfo* url_GetNext(RandState rand) {
  if (urlCount == 0) {
    return NULL;
  }
  if (urlCount == 1) {
    return &(urls[0]);
  }

  const long randVal = apib_Rand(rand);
  return &(urls[randVal % urlCount]);
}

void url_Reset() {
  if (initialized) {
    for (int i = 0; i < urlCount; i++) {
      free(urls[i].path);
      if (urls[i].addresses != NULL) {
        freeaddrinfo(urls[i].addresses);
      }
    }
    urlCount = urlSize = 0;
    free(urls);
    initialized = 0;
  }
}