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
#include <errno.h>
#include <openssl/err.h>
#include <openssl/ssl.h>
#include <unistd.h>

#include "src/apib_iothread.h"

static void printSslError(const char* msg, int err) {
  char buf[256];
  ERR_error_string_n(err, buf, 256);
  printf("%s: %s\n", msg, buf);
}

IOStatus io_Write(ConnectionState* c, const void* buf, size_t count,
                  size_t* written) {
  if (c->ssl == NULL) {
    const ssize_t ws = write(c->fd, buf, count);
    if (ws > 0) {
      *written = ws;
      return OK;
    }
    if (ws == 0) {
      *written = 0;
      return FEOF;
    }

    if ((errno == EAGAIN) || (errno == EWOULDBLOCK)) {
      return NEED_WRITE;
    }
    return SOCKET_ERROR;
  }

  const int s = SSL_write_ex(c->ssl, buf, count, written);
  if (s == 1) {
    return OK;
  }
  const int sslErr = SSL_get_error(c->ssl, s);
  if (sslErr == SSL_ERROR_WANT_READ) {
    return NEED_READ;
  }
  if (sslErr == SSL_ERROR_WANT_WRITE) {
    return NEED_WRITE;
  }
  printSslError("TLS write error", sslErr);
  return TLS_ERROR;
}

IOStatus io_Read(ConnectionState* c, void* buf, size_t count, size_t* readed) {
  if (c->ssl == NULL) {
    const ssize_t rs = read(c->fd, buf, count);
    if (rs > 0) {
      *readed = rs;
      return OK;
    }
    if (rs == 0) {
      *readed = 0;
      return FEOF;
    }

    if ((errno == EAGAIN) || (errno == EWOULDBLOCK)) {
      return NEED_READ;
    }
    return SOCKET_ERROR;
  }

  const int s = SSL_read_ex(c->ssl, buf, count, readed);
  if (s == 1) {
    return OK;
  }
  const int sslErr = SSL_get_error(c->ssl, s);
  if (sslErr == SSL_ERROR_WANT_READ) {
    return NEED_READ;
  }
  if (sslErr == SSL_ERROR_WANT_WRITE) {
    return NEED_WRITE;
  }
  printSslError("TLS read error", sslErr);
  return TLS_ERROR;
}