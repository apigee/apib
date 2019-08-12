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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <pthread.h>

#include "test/test_server.h"

#include "src/apib_lines.h"
#include "src/apib_message.h"

#define BACKLOG 32
#define READ_BUF 512

typedef struct {
  int fd;
} RequestInfo;

static void sendText(int fd, int code, const char* codestr, const char* msg) {
  StringBuf buf;
  
  buf_New(&buf, 0);
  buf_Printf(&buf, "HTTP/1.1 %i %s\r\n", code, codestr);
  buf_Append(&buf, "Server: apib test server\r\n");
  buf_Append(&buf, "Content-Type: text/plain\r\n");
  buf_Printf(&buf, "Content-Length: %lu\r\n", strlen(msg));
  buf_Append(&buf, "\r\n");
  buf_Append(&buf, msg);

  write(fd, buf_Get(&buf), buf_Length(&buf));
  buf_Free(&buf);
}

static void* requestThread(void* a) {
  RequestInfo* i = (RequestInfo*)a;
  LineState lines;
  char* buf = (char*)malloc(READ_BUF);
  linep_Start(&lines, buf, READ_BUF, 0);
  linep_SetHttpMode(&lines, 1);

  HttpMessage* m = message_NewRequest();

  do {
    const int readCount = linep_ReadFd(&lines, i->fd);
    if (readCount < 0) {
      perror("Error on read from socket");
      goto finish;
    } else if (readCount == 0) {
      // EOF
      break;
    }

    int err = message_Fill(m, &lines);
    if (err != 0) {
      fprintf(stderr, "Error parsing HTTP request: %i", err);
      // TODO should we return 400?
      goto finish;
    }
    err = linep_Reset(&lines);
    if (err != 0) {
      fprintf(stderr, "HTTP lines too long");
      goto finish;
    }
  } while (m->state != MESSAGE_DONE);
 
  if (!strcmp("/hello", m->path)) {
    if (!strcmp("GET", m->method)) {
      sendText(i->fd, 200, "OK", "Hello, World!\n");
    } else {
      sendText(i->fd, 405, "BAD METHOD", "Wrong method");
    }
  }
  sendText(i->fd, 404, "NOT FOUND", "Not found");
 
finish:
  close(i->fd);
  message_Free(m);
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
    //pthread_attr_t threadAttrs;
    //pthread_attr_init(&threadAttrs);
    pthread_create(&thread, NULL, requestThread, i);
    //pthread_attr_destroy(&threadAttrs);
    pthread_detach(thread);
  }
}

int testserver_Start(TestServer* s, int port) {
  message_Init();

  s->listenfd = socket(AF_INET, SOCK_STREAM, 0);
  if (s->listenfd < 0) {
    perror("Cant' create socket");
    return -1;
  }

  struct sockaddr_in addr;
  addr.sin_family = AF_INET;
  addr.sin_port = htons(port);
  addr.sin_addr.s_addr = htonl(INADDR_ANY);

  int err = bind(s->listenfd, (const struct sockaddr*)&addr, sizeof(struct sockaddr_in));
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
