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

/*
 * This is a program that returns CPU information over the network.
 */

#include "src/apib_mon.h"

#include <arpa/inet.h>
#include <assert.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include "src/apib_cpu.h"
#include "src/apib_lines.h"

#define LISTEN_BACKLOG 8
#define READ_BUF_LEN 128
#define PROC_BUF_LEN 512

typedef struct {
  int fd;
} ConnInfo;

static void sendBack(ConnInfo* i, const char* msg) {
  const size_t len = strlen(msg);
  write(i->fd, msg, len);
}

static int processCommand(ConnInfo* i, const char* cmd, CPUUsage* lastUsage) {
  char buf[READ_BUF_LEN];

  if (!strcasecmp(cmd, "HELLO")) {
    sendBack(i, "Hi!\n");

  } else if (!strcasecmp(cmd, "CPU")) {
    double usage = cpu_GetInterval(lastUsage);
    assert(snprintf(buf, READ_BUF_LEN, "%.2lf\n", usage) < READ_BUF_LEN);
    sendBack(i, buf);

  } else if (!strcasecmp(cmd, "MEM")) {
    double usage = cpu_GetMemoryUsage();
    assert(snprintf(buf, READ_BUF_LEN, "%.2lf\n", usage) < READ_BUF_LEN);
    sendBack(i, buf);

  } else if (!strcasecmp(cmd, "BYE") || !strcasecmp(cmd, "QUIT")) {
    sendBack(i, "BYE\n");
    return 1;

  } else {
    sendBack(i, "Invalid command\n");
  }
  return 0;
}

static void* socketThread(void* a) {
  ConnInfo* i = (ConnInfo*)a;
  char* readBuf = malloc(READ_BUF_LEN);
  int closeRequested = 0;
  CPUUsage lastUsage;
  LineState line;

  cpu_GetUsage(&lastUsage);
  linep_Start(&line, readBuf, READ_BUF_LEN, 0);

  while (!closeRequested) {
    int s = linep_ReadFd(&line, i->fd);
    if (s <= 0) {
      break;
    }
    while (!closeRequested && linep_NextLine(&line)) {
      char* l = linep_GetLine(&line);
      closeRequested = processCommand(i, l, &lastUsage);
    }
    if (!closeRequested) {
      if (linep_Reset(&line)) {
        /* Line too big to fit in buffer -- abort */
        break;
      }
    }
  }

  close(i->fd);
  free(readBuf);
  free(i);

  return NULL;
}

static void* acceptThread(void* arg) {
  MonServer* s = (MonServer*)arg;

  for (;;) {
    const int fd = accept(s->listenfd, NULL, NULL);
    if (fd < 0) {
      // This could be because the socket was closed.
      perror("Error accepting socket");
      return NULL;
    }

    ConnInfo* i = (ConnInfo*)malloc(sizeof(ConnInfo));
    i->fd = fd;

    pthread_t thread;
    pthread_create(&thread, NULL, socketThread, i);
    pthread_detach(thread);
  }
}

int mon_StartServer(MonServer* s, const char* address, int port) {
  memset(s, 0, sizeof(MonServer));

  int err = cpu_Init();
  if (err != 0) {
    fprintf(stderr, "CPU monitoring not available on this platform\n");
    return -3;
  }

  s->listenfd = socket(AF_INET, SOCK_STREAM, 0);
  if (s->listenfd < 0) {
    perror("Cant' create socket");
    return -1;
  }

  struct sockaddr_in addr;
  addr.sin_family = AF_INET;
  addr.sin_port = htons(port);
  // Listen on localhost to avoid weird firewall stuff on Macs
  // We may have to revisit if we test on platforms with a different address.
  addr.sin_addr.s_addr = inet_addr(address);

  err = bind(s->listenfd, (const struct sockaddr*)&addr,
             sizeof(struct sockaddr_in));
  if (err != 0) {
    perror("Can't bind to port");
    return -2;
  }

  err = listen(s->listenfd, LISTEN_BACKLOG);
  if (err != 0) {
    perror("Can't listen on socket");
    close(s->listenfd);
    return -3;
  }

  err = pthread_create(&(s->acceptThread), NULL, acceptThread, s);
  assert(err == 0);

  return 0;
}

int mon_GetPort(const MonServer* s) {
  struct sockaddr_in addr;
  socklen_t addrlen = sizeof(struct sockaddr_in);

  getsockname(s->listenfd, (struct sockaddr*)&addr, &addrlen);
  return ntohs(addr.sin_port);
}

void mon_StopServer(MonServer* s) {
  pthread_cancel(s->acceptThread);
  void* ret;
  pthread_join(s->acceptThread, &ret);
  close(s->listenfd);
}

void mon_JoinServer(MonServer* s) {
  void* ret;
  pthread_join(s->acceptThread, &ret);
}