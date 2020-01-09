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

#include <openssl/err.h>
#include <openssl/ssl.h>
#include <unistd.h>

#include <cassert>

#include "apib/apib_iothread.h"

namespace apib {

IOStatus ConnectionState::doWrite(const void* buf, size_t count,
                                  size_t* written) {
  assert(written != NULL);
  if (ssl_ == NULL) {
    const ssize_t ws = write(fd_, buf, count);
    if (ws > 0) {
      *written = ws;
      return OK;
    }

    *written = 0;
    if (ws == 0) {
      return FEOF;
    }
    if ((errno == EAGAIN) || (errno == EWOULDBLOCK)) {
      return NEED_WRITE;
    }
    return SOCKET_ERROR;
  }

  const int s = SSL_write(ssl_, buf, count);
  io_Verbose(this, "SSL write(%zu) returned %i\n", count, s);
  if (s > 0) {
    *written = s;
    return OK;
  }

  // Man page says that "0" means "failure".
  *written = 0;
  const int sslErr = SSL_get_error(ssl_, s);
  if (sslErr == SSL_ERROR_WANT_READ) {
    return NEED_READ;
  }
  if (sslErr == SSL_ERROR_WANT_WRITE) {
    return NEED_WRITE;
  }
  printSslError("TLS write error", sslErr);
  return TLS_ERROR;
}

IOStatus ConnectionState::doRead(void* buf, size_t count, size_t* readed) {
  assert(readed != NULL);
  if (ssl_ == NULL) {
    const ssize_t rs = read(fd_, buf, count);
    if (rs > 0) {
      *readed = rs;
      return OK;
    }

    *readed = 0;
    if (rs == 0) {
      return FEOF;
    }
    if ((errno == EAGAIN) || (errno == EWOULDBLOCK)) {
      return NEED_READ;
    }
    return SOCKET_ERROR;
  }

  const int s = SSL_read(ssl_, buf, count);
  io_Verbose(this, "SSL read(%zu) returned %i\n", count, s);
  if (s > 0) {
    *readed = s;
    return OK;
  }

  *readed = 0;
  int sentShutdown = (SSL_get_shutdown(ssl_) & SSL_SENT_SHUTDOWN);

  const int sslErr = SSL_get_error(ssl_, s);
  io_Verbose(this, "SSL read = %i, error = %i\n", s, sslErr);
  switch (sslErr) {
    case SSL_ERROR_WANT_READ:
      return NEED_READ;
    case SSL_ERROR_WANT_WRITE:
      return NEED_WRITE;
    case SSL_ERROR_ZERO_RETURN:
      return FEOF;
    case SSL_ERROR_SYSCALL:
      // After a shutdown, SSL docs say that we get this. Ignore it.
      if (sentShutdown) {
        return FEOF;
      }
      printSslError("Error on read", sslErr);
      return TLS_ERROR;
    default:
      printSslError("TLS read error", sslErr);
      return TLS_ERROR;
  }
}

IOStatus ConnectionState::doClose() {
  if (ssl_ == NULL) {
    const int err = close(fd_);
    io_Verbose(this, "Close returned %i\n", err);
    return (err == 0 ? OK : SOCKET_ERROR);
  }

  const int s = SSL_shutdown(ssl_);
  io_Verbose(this, "SSL_shutdown returned %i\n", s);
  if (s == 1) {
    return OK;
  }
  if (s == 0) {
    // Docs say that we must call SSL_read here.
    size_t readed;
    return doRead(NULL, 0, &readed);
  }

  const int sslErr = SSL_get_error(ssl_, s);
  switch (sslErr) {
    case SSL_ERROR_WANT_READ:
      return NEED_READ;
    case SSL_ERROR_WANT_WRITE:
      return NEED_WRITE;
    default:
      printSslError("TLS shutdown error", sslErr);
      return TLS_ERROR;
  }
}

void ConnectionState::Reset() {
  if (ssl_ != NULL) {
    SSL_free(ssl_);
    ssl_ = NULL;
  }
  close(fd_);
}

}  // namespace apib