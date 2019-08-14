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

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <pthread.h>
#include <regex.h>

#include "test/test_server.h"

#include "src/apib_lines.h"

#include "http_parser.h"

#define BACKLOG 32
#define READ_BUF 1024

static http_parser_settings ParserSettings;

static regex_t sizeParameter;

#define SIZE_PARAMETER_REGEX "^size=([0-9]+)"

typedef struct {
  int fd;
  int done;
  char* path;
  int queryLen;
  char** query;
  http_parser parser;
} RequestInfo;

static char* makeData(const int len) {
  char* b = (char*)malloc(len);
  for (int i = 0; i < len; i++) {
    b[i] = '0' + (i % 10);
  }
  return b;
}

static void sendText(int fd, int code, const char* codestr, const char* msg) {
  StringBuf buf;
  
  buf_New(&buf, 0);
  buf_Printf(&buf, "HTTP/1.1 %i %s\r\n", code, codestr);
  buf_Append(&buf, "Server: apib test server\r\n");
  buf_Append(&buf, "Content-Type: text/plain\r\n");
  buf_Printf(&buf, "Content-Length: %lu\r\n", strlen(msg));
  buf_Append(&buf, "Connection: close\r\n");
  buf_Append(&buf, "\r\n");
  buf_Append(&buf, msg);

  write(fd, buf_Get(&buf), buf_Length(&buf));
  buf_Free(&buf);
}

static void sendData(int fd, int len) {
  StringBuf buf;
  
  buf_New(&buf, 0);
  buf_Append(&buf, "HTTP/1.1 200 OK\r\n");
  buf_Append(&buf, "Server: apib test server\r\n");
  buf_Append(&buf, "Content-Type: text/plain\r\n");
  buf_Printf(&buf, "Content-Length: %i\r\n", len);
  buf_Append(&buf, "Connection: close\r\n");
  buf_Append(&buf, "\r\n");

  write(fd, buf_Get(&buf), buf_Length(&buf));
  buf_Free(&buf);

  char* data = makeData(len);
  write(fd, data, len);
  free(data);
}

// Called by http_parser when we get the URL.
static int parsedUrl(http_parser* p, const char* buf, size_t len) {
  RequestInfo* i = (RequestInfo*)p->data;
  char* url = (char*)malloc(len + 1);
  memcpy(url, buf, len);
  url[len] = 0;

  char* sp;
  char* t = strtok_r(url, "?", &sp);
  i->path = strdup(t);
  t = strtok_r(NULL, "?", &sp);

  if (t == NULL) {
    i->queryLen = 0;
    i->query = NULL;
  } else {
    // Going to run strtok twice -- first time to count
    char* query1 = strdup(t);
    char* query2 = strdup(t);

    int queryLen = 0;
    t = strtok_r(query1, "&", &sp);
    while (t != NULL) {
      queryLen++;
      t = strtok_r(NULL, "&", &sp);
    }

    i->queryLen = queryLen;
    i->query = (char**)malloc(sizeof(char*) * queryLen);

    t = strtok_r(query2, "&", &sp);
    for (int inc = 0; inc < queryLen; inc++) {
      assert(t != NULL);
      i->query[inc] = strdup(t);
      t = strtok_r(NULL, "&", &sp);
    }

    free(query1);
    free(query2);
  }

  free(url);
  return 0;
}

static int parseComplete(http_parser* p) {
  RequestInfo* i = (RequestInfo*)p->data;
  i->done = 1;
  return 0;
}

static void handleRequest(RequestInfo* i) {
  if (!strcmp("/hello", i->path)) {
    if (i->parser.method == HTTP_GET) {
      sendText(i->fd, 200, "OK", "Hello, World!\n");
    } else {
      sendText(i->fd, 405, "BAD METHOD", "Wrong method");
    }
  
  } else if (!strcmp("/data", i->path)) {
    if (i->parser.method == HTTP_GET) {
      int size = 1024;
      for (int j = 0; j < i->queryLen; j++) {
        regmatch_t matches[2];
        if (regexec(&sizeParameter, i->query[j], 2, matches, 0) == 0) {
          assert(matches[1].rm_so < matches[1].rm_eo);
          char* tmp = strndup(i->query[j] + matches[1].rm_so, matches[1].rm_eo - matches[1].rm_so);
          size = atoi(tmp);
          free(tmp);
        }
      }

      sendData(i->fd, size);
    } else {
      sendText(i->fd, 405, "BAD METHOD", "Wrong method");
    }
  }
  sendText(i->fd, 404, "NOT FOUND", "Not found");
}

static void* requestThread(void* a) {
  RequestInfo* i = (RequestInfo*)a;
  char* buf = (char*)malloc(READ_BUF);
  size_t bufPos = 0;

  i->done = 0;
  http_parser_init(&(i->parser), HTTP_REQUEST);
  i->parser.data = i;

  do {
    const ssize_t readCount = read(i->fd, buf + bufPos, READ_BUF - bufPos);
    if (readCount < 0) {
      perror("Error on read from socket");
      goto finish;
    } else if (readCount == 0) {
      // EOF
      break;
    }

    const size_t parseCount = http_parser_execute(&(i->parser), &ParserSettings, 
      buf, readCount);
    if (i->parser.http_errno != 0) {
      fprintf(stderr, "Error parsing HTTP request: %i\n", i->parser.http_errno);
      goto finish;
    }
    bufPos += parseCount;

    if (parseCount < readCount) {
      memmove(buf, buf + parseCount, readCount - parseCount);
      bufPos = readCount - parseCount;
    }
  } while (!i->done);
 
  handleRequest(i);
 
finish:
  close(i->fd);
  free(buf);
  free(i->path);
  if (i->queryLen > 0) {
    for (int inc = 0; inc < i->queryLen; inc++) {
      free(i->query[inc]);
    }
    free(i->query);
  }
  free(i);
  return NULL;
}

static void* acceptThread(void* a) {
  TestServer* s = (TestServer*)a;

  for (;;) {
    const int fd = accept(s->listenfd, NULL, NULL);
    if (fd < 0) {
      // This could be because the socket was closed.
      perror("Error accepting socket");
      return NULL;
    }
    
    RequestInfo* i = (RequestInfo*)malloc(sizeof(RequestInfo));
    i->fd = fd;

    pthread_t thread;
    pthread_create(&thread, NULL, requestThread, i);
    pthread_detach(thread);
  }
}

int testserver_Start(TestServer* s, int port) {
  int err = regcomp(&sizeParameter, SIZE_PARAMETER_REGEX, REG_EXTENDED);
  assert(err == 0);

  http_parser_settings_init(&ParserSettings);
  ParserSettings.on_url = parsedUrl;
  ParserSettings.on_message_complete = parseComplete;

  s->listenfd = socket(AF_INET, SOCK_STREAM, 0);
  if (s->listenfd < 0) {
    perror("Cant' create socket");
    return -1;
  }

  struct sockaddr_in addr;
  addr.sin_family = AF_INET;
  addr.sin_port = htons(port);
  addr.sin_addr.s_addr = htonl(INADDR_ANY);

  err = bind(s->listenfd, (const struct sockaddr*)&addr, sizeof(struct sockaddr_in));
  if (err != 0) {
    perror("Can't bind to port");
    return -2;
  }

  err = listen(s->listenfd, BACKLOG);
  if (err != 0) {
    perror("Can't listen on socket");
    return -3;
  }

  pthread_attr_t threadAttrs;
  pthread_attr_init(&threadAttrs);
  pthread_create(&(s->acceptThread), &threadAttrs, acceptThread, s);
  pthread_attr_destroy(&threadAttrs);

  return 0;
}

int testserver_GetPort(TestServer* s) {
  struct sockaddr_in addr;
  socklen_t addrlen = sizeof(struct sockaddr_in);

  getsockname(s->listenfd, (struct sockaddr*)&addr, &addrlen);
  return ntohs(addr.sin_port);
}

void testserver_Stop(TestServer* s) {
  close(s->listenfd);
}

void testserver_Join(TestServer* s) {
  void* ret;
  pthread_join(s->acceptThread, &ret);
  pthread_detach(s->acceptThread);
}
