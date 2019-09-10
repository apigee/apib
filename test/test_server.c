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

#include "test/test_server.h"

#include <arpa/inet.h>
#include <assert.h>
#include <netinet/in.h>
#include <openssl/err.h>
#include <openssl/ssl.h>
#include <pthread.h>
#include <regex.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include "http_parser.h"
#include "src/apib_lines.h"

#define BACKLOG 32
#define READ_BUF 1024

static http_parser_settings ParserSettings;

static regex_t sizeParameter;

#define SIZE_PARAMETER_REGEX "^size=([0-9]+)"

typedef struct {
  TestServer* server;
  int fd;
  int done;
  char* path;
  size_t queryLen;
  char** query;
  char* body;
  size_t bodyLen;
  char* nextHeader;
  int shouldClose;
  int notAuthorized;
  int sleepTime;
  http_parser parser;
  SSL* ssl;
} RequestInfo;

static char* makeData(const int len) {
  char* b = (char*)malloc(len);
  for (int i = 0; i < len; i++) {
    b[i] = '0' + (i % 10);
  }
  return b;
}

static void printSslError(const char* msg) {
  char buf[256];
  ERR_error_string_n(ERR_get_error(), buf, 256);
  fprintf(stderr, "%s: %s\n", msg, buf);
}

static void success(RequestInfo* i, int op) {
  assert(op < NUM_OPS);
  pthread_mutex_lock(&(i->server->statsLock));
  i->server->stats.successes[op]++;
  i->server->stats.successCount++;
  pthread_mutex_unlock(&(i->server->statsLock));
}

static void failure(RequestInfo* i) {
  pthread_mutex_lock(&(i->server->statsLock));
  i->server->stats.errorCount++;
  pthread_mutex_unlock(&(i->server->statsLock));
}

static int doWrite(RequestInfo* i, const void* buf, size_t len) {
  if (i->ssl == NULL) {
    return write(i->fd, buf, len);
  }
  return SSL_write(i->ssl, buf, len);
}

static void sendText(RequestInfo* i, int code, const char* codestr,
                     const char* msg) {
  StringBuf buf;

  buf_New(&buf, 0);
  buf_Printf(&buf, "HTTP/1.1 %i %s\r\n", code, codestr);
  buf_Append(&buf, "Server: apib test server\r\n");
  buf_Append(&buf, "Content-Type: text/plain\r\n");
  buf_Printf(&buf, "Content-Length: %lu\r\n", strlen(msg));
  buf_Append(&buf, "\r\n");
  buf_Append(&buf, msg);

  doWrite(i, buf_Get(&buf), buf_Length(&buf));
  buf_Free(&buf);
}

static void sendData(RequestInfo* i, char* data, size_t len) {
  StringBuf buf;

  buf_New(&buf, 0);
  buf_Append(&buf, "HTTP/1.1 200 OK\r\n");
  buf_Append(&buf, "Server: apib test server\r\n");
  buf_Append(&buf, "Content-Type: text/plain\r\n");
  buf_Printf(&buf, "Content-Length: %i\r\n", len);
  buf_Append(&buf, "\r\n");

  doWrite(i, buf_Get(&buf), buf_Length(&buf));
  buf_Free(&buf);

  doWrite(i, data, len);
}

// Called by http_parser to collect the request body
static int parsedBody(http_parser* p, const char* buf, size_t len) {
  RequestInfo* i = (RequestInfo*)p->data;
  if (i->body == NULL) {
    i->bodyLen = len;
    i->body = (char*)malloc(len);
    memcpy(i->body, buf, len);
  } else {
    const size_t newLen = i->bodyLen + len;
    i->body = (char*)realloc(i->body, newLen);
    memcpy(i->body + i->bodyLen, buf, len);
    i->bodyLen = newLen;
  }
  return 0;
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

static int parsedHeaderField(http_parser* p, const char* buf, size_t len) {
  RequestInfo* i = (RequestInfo*)p->data;
  i->nextHeader = strndup(buf, len);
  return 0;
}

static int parsedHeaderValue(http_parser* p, const char* buf, size_t len) {
  RequestInfo* i = (RequestInfo*)p->data;
  if (!strcasecmp("Connection", i->nextHeader) &&
      !strncasecmp("close", buf, len)) {
    i->shouldClose = 1;
  } else if (!strcasecmp("Authorization", i->nextHeader) &&
             strncmp("Basic dGVzdDp2ZXJ5dmVyeXNlY3JldA==", buf, len)) {
    // Checked for the authorization "test:veryverysecret"
    i->notAuthorized = 1;
  } else if (!strcasecmp("X-Sleep", i->nextHeader)) {
    char* tmpSleep = strndup(buf, len);
    i->sleepTime = atoi(tmpSleep);
    free(tmpSleep);
  }
  free(i->nextHeader);
  return 0;
}

static void handleRequest(RequestInfo* i) {
  if (i->notAuthorized) {
    sendText(i, 401, "Not authorized", "Wrong password!\n");
    return;
  }

  if (i->sleepTime > 0) {
    sleep(i->sleepTime);
  }

  if (!strcmp("/hello", i->path)) {
    if (i->parser.method == HTTP_GET) {
      sendText(i, 200, "OK", "Hello, World!\n");
      success(i, OP_HELLO);
    } else {
      sendText(i, 405, "BAD METHOD", "Wrong method");
      failure(i);
    }

  } else if (!strcmp("/data", i->path)) {
    if (i->parser.method == HTTP_GET) {
      int size = 1024;
      for (int j = 0; j < i->queryLen; j++) {
        regmatch_t matches[2];
        if (regexec(&sizeParameter, i->query[j], 2, matches, 0) == 0) {
          assert(matches[1].rm_so < matches[1].rm_eo);
          char* tmp = strndup(i->query[j] + matches[1].rm_so,
                              matches[1].rm_eo - matches[1].rm_so);
          size = atoi(tmp);
          free(tmp);
        }
      }
      char* data = makeData(size);
      sendData(i, data, size);
      free(data);
      success(i, OP_DATA);
    } else {
      sendText(i, 405, "BAD METHOD", "Wrong method");
      failure(i);
    }

  } else if (!strcmp("/echo", i->path)) {
    if (i->parser.method == HTTP_POST) {
      sendData(i, i->body, i->bodyLen);
      success(i, OP_ECHO);
    } else {
      sendText(i, 405, "BAD METHOD", "Wrong method");
      failure(i);
    }
  } else {
    sendText(i, 404, "NOT FOUND", "Not found");
    failure(i);
  }
}

static ssize_t httpTransaction(RequestInfo* i, char* buf, ssize_t bufPos) {
  i->done = 0;
  http_parser_init(&(i->parser), HTTP_REQUEST);
  i->parser.data = i;

  do {
    int readCount;
    if (i->ssl == NULL) {
      readCount = read(i->fd, buf + bufPos, READ_BUF - bufPos);
    } else {
      readCount = SSL_read(i->ssl, buf + bufPos, READ_BUF - bufPos);
    }

    if (readCount < 0) {
      if (i->ssl == NULL) {
        perror("Error on read from socket");
      } else {
        printSslError("Error on socket read");
      }
      pthread_mutex_lock(&(i->server->statsLock));
      i->server->stats.socketErrorCount++;
      pthread_mutex_unlock(&(i->server->statsLock));
      return -1;
    } else if (readCount == 0) {
      return -2;
    }

    const size_t available = bufPos + readCount;
    const size_t parseCount =
        http_parser_execute(&(i->parser), &ParserSettings, buf, available);

    if (i->parser.http_errno != 0) {
      fprintf(stderr, "Error parsing HTTP request: %i: %s\n",
              i->parser.http_errno,
              http_errno_description(i->parser.http_errno));
      return -1;
    }

    if (parseCount < available) {
      const size_t leftover = available - parseCount;
      memmove(buf, buf + parseCount, leftover);
      bufPos = leftover;
    } else {
      bufPos = 0;
    }
  } while (!i->done);

  handleRequest(i);
  if (i->shouldClose) {
    return -1;
  }
  return bufPos;
}

static void* requestThread(void* a) {
  RequestInfo* i = (RequestInfo*)a;
  char* buf = (char*)malloc(READ_BUF);
  ssize_t bufPos = 0;

  if (i->server->sslCtx != NULL) {
    i->ssl = SSL_new(i->server->sslCtx);
    int err = SSL_set_fd(i->ssl, i->fd);
    if (err != 1) {
      printSslError("Can't connect to SSL FD");
      goto finish;
    }
    SSL_set_accept_state(i->ssl);
  }

  pthread_mutex_lock(&(i->server->statsLock));
  i->server->stats.connectionCount++;
  pthread_mutex_unlock(&(i->server->statsLock));

  do {
    bufPos = httpTransaction(i, buf, bufPos);
    if (i->path != NULL) {
      free(i->path);
      i->path = NULL;
    }
    if (i->queryLen > 0) {
      for (int inc = 0; inc < i->queryLen; inc++) {
        free(i->query[inc]);
      }
      free(i->query);
      i->queryLen = 0;
      i->query = NULL;
    }
    if (i->body != NULL) {
      free(i->body);
      i->bodyLen = 0;
      i->body = NULL;
    }
  } while (bufPos >= 0);

finish:
  close(i->fd);
  free(buf);
  if (i->ssl != NULL) {
    SSL_free(i->ssl);
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

    RequestInfo* i = (RequestInfo*)calloc(1, sizeof(RequestInfo));
    i->server = s;
    i->fd = fd;

    pthread_t thread;
    pthread_create(&thread, NULL, requestThread, i);
    pthread_detach(thread);
  }
}

static int initializeSSL(TestServer* s, const char* keyFile,
                         const char* certFile) {
  s->sslCtx = SSL_CTX_new(TLS_server_method());

  int err = SSL_CTX_use_certificate_chain_file(s->sslCtx, certFile);
  if (err != 1) {
    printSslError("Can't load certificate file");
    return -1;
  }

  err = SSL_CTX_use_PrivateKey_file(s->sslCtx, keyFile, SSL_FILETYPE_PEM);
  if (err != 1) {
    printSslError("Can't load key file");
    return -2;
  }

  return 0;
}

int testserver_Start(TestServer* s, const char* address, int port, const char* keyFile,
                     const char* certFile) {
  int err = regcomp(&sizeParameter, SIZE_PARAMETER_REGEX, REG_EXTENDED);
  assert(err == 0);

  http_parser_settings_init(&ParserSettings);
  ParserSettings.on_url = parsedUrl;
  ParserSettings.on_header_field = parsedHeaderField;
  ParserSettings.on_header_value = parsedHeaderValue;
  ParserSettings.on_body = parsedBody;
  ParserSettings.on_message_complete = parseComplete;

  memset(s, 0, sizeof(TestServer));
  pthread_mutex_init(&(s->statsLock), NULL);

  if ((keyFile != NULL) && (certFile != NULL)) {
    if (initializeSSL(s, keyFile, certFile) != 0) {
      return -1;
    }
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

void testserver_GetStats(TestServer* s, TestServerStats* stats) {
  pthread_mutex_lock(&(s->statsLock));
  memcpy(stats, &(s->stats), sizeof(TestServerStats));
  pthread_mutex_unlock(&(s->statsLock));
}

void testserver_ResetStats(TestServer* s) {
  pthread_mutex_lock(&(s->statsLock));
  memset(&(s->stats), 0, sizeof(TestServerStats));
  pthread_mutex_unlock(&(s->statsLock));
}

void testserver_Stop(TestServer* s) { close(s->listenfd); }

void testserver_Join(TestServer* s) {
  void* ret;
  pthread_join(s->acceptThread, &ret);
  pthread_detach(s->acceptThread);
  if (s->sslCtx != NULL) {
    SSL_CTX_free(s->sslCtx);
  }
}
