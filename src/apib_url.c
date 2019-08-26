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

#include <arpa/inet.h>
#include <assert.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>

#include "http_parser.h"
#include "src/apib_lines.h"

#define URL_BUF_LEN 8192
#define INITIAL_URLS 16

static unsigned int urlCount = 0;
static unsigned int urlSize = 0;
static URLInfo* urls;
static int initialized = 0;

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

  // Count results
  struct addrinfo* a = results;
  int c = 0;
  while (a != NULL) {
    c++;
    a = a->ai_next;
  }

  u->addresses =
      (struct sockaddr_storage*)malloc(sizeof(struct sockaddr_storage) * c);
  u->addressLengths = (size_t*)malloc(sizeof(size_t) * c);
  u->addressCount = c;

  // Copy results to more permanent storage
  a = results;
  for (int i = 0; a != NULL; i++) {
    memcpy(&(u->addresses[i]), a->ai_addr, a->ai_addrlen);
    u->addressLengths[i] = a->ai_addrlen;
    // IP4 and IP6 versions of this should have port in same place
    ((struct sockaddr_in*)&(u->addresses[i]))->sin_port = htons(u->port);
    a = a->ai_next;
  }

  freeaddrinfo(results);

  return 0;
}

static int compareUrlPart(const struct http_parser_url* pu, const char* urlstr,
                          const char* str, int part) {
  return strncmp(urlstr + pu->field_data[part].off, str,
                 pu->field_data[part].len);
}

static char* copyUrlPart(const struct http_parser_url* pu, const char* urlstr,
                         int part) {
  return strndup(urlstr + pu->field_data[part].off, pu->field_data[part].len);
}

static void appendUrlPart(const struct http_parser_url* pu, const char* urlstr,
                          int part, StringBuf* buf) {
  buf_AppendN(buf, urlstr + pu->field_data[part].off, pu->field_data[part].len);
}

static int initUrl(const char* urlstr, URLInfo* u) {
  struct http_parser_url pu;

  http_parser_url_init(&pu);
  const int err = http_parser_parse_url(urlstr, strlen(urlstr), 0, &pu);
  if (err != 0) {
    fprintf(stderr, "Invalid URL: \"%s\"\n", urlstr);
    return -1;
  }

  if (!(pu.field_set & (1 << UF_SCHEMA)) || !(pu.field_set & (1 << UF_HOST))) {
    fprintf(stderr, "Error matching URL: %s\n", urlstr);
    return -2;
  }

  if (!compareUrlPart(&pu, urlstr, "http", UF_SCHEMA)) {
    u->isSsl = 0;
  } else if (!compareUrlPart(&pu, urlstr, "https", UF_SCHEMA)) {
    u->isSsl = 1;
  } else {
    fprintf(stderr, "Invalid URL scheme: \"%s\"\n", urlstr);
    return -3;
  }

  char* hoststr = copyUrlPart(&pu, urlstr, UF_HOST);
  assert(hoststr != NULL);

  if (pu.field_set & (1 << UF_PORT)) {
    u->port = pu.port;
  } else if (u->isSsl) {
    u->port = 443;
  } else {
    u->port = 80;
  }

  StringBuf pathBuf;
  buf_New(&pathBuf, 0);

  if (pu.field_set & (1 << UF_PATH)) {
    appendUrlPart(&pu, urlstr, UF_PATH, &pathBuf);
  } else {
    buf_Append(&pathBuf, "/");
  }

  if (pu.field_set & (1 << UF_QUERY)) {
    buf_Append(&pathBuf, "?");
    appendUrlPart(&pu, urlstr, UF_QUERY, &pathBuf);
  }

  if (pu.field_set & (1 << UF_FRAGMENT)) {
    buf_Append(&pathBuf, "#");
    appendUrlPart(&pu, urlstr, UF_FRAGMENT, &pathBuf);
  }

  // Copy the final buffer...
  // No need to free what was previously allocated by "buf_New"!
  u->path = buf_Get(&pathBuf);

  // Calculate the host header properly
  if (((u->isSsl) && (u->port == 443)) || ((!u->isSsl) && (u->port == 80))) {
    u->hostHeader = strdup(hoststr);
  } else {
    u->hostHeader = malloc(strlen(hoststr) + 20);
    sprintf(u->hostHeader, "%s:%i", hoststr, u->port);
  }

  // Now look up the host and add the port...
  const int hosterr = initHost(hoststr, u);
  free(hoststr);
  if (hosterr) {
    // No addresses, which is OK now
    u->addresses = NULL;
    u->addressCount = 0;
  }

  return 0;
}

int url_InitOne(const char* urlStr) {
  assert(!initialized);

  urlCount = urlSize = 1;
  urls = (URLInfo*)malloc(sizeof(URLInfo));
  const int e = initUrl(urlStr, &(urls[0]));
  if (e == 0) {
    initialized = 1;
  }
  return e;
}

struct sockaddr* url_GetAddress(const URLInfo* url, int index, size_t* len) {
  const int ix = index % url->addressCount;
  if (len != NULL) {
    *len = url->addressLengths[ix];
  }
  return (struct sockaddr*)&(url->addresses[ix]);
}

int url_IsSameServer(const URLInfo* u1, const URLInfo* u2, int index) {
  if (u1->addressCount != u2->addressCount) {
    return -1;
  }
  const int ix = index % u1->addressCount;
  if (u1->addressLengths[ix] != u2->addressLengths[ix]) {
    return -1;
  }
  return !memcmp(&(u1->addresses[ix]), &(u2->addresses[ix]),
                 u1->addressLengths[ix]);
}

int url_InitFile(const char* fileName) {
  assert(!initialized);

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

URLInfo* url_GetNext(RandState rand) {
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
      free(urls[i].hostHeader);
      free(urls[i].addresses);
      free(urls[i].addressLengths);
    }
    urlCount = urlSize = 0;
    free(urls);
    initialized = 0;
  }
}