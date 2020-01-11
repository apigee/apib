/*
Copyright 2020 Google LLC

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

#include "apib/tlssocket.h"

#include <unistd.h>

#include <cassert>

#include "openssl/err.h"

namespace apib {

static constexpr int kSslErrLen = 256;

static Status makeTLSError(int err) {
  char buf[kSslErrLen];
  ERR_error_string_n(err, buf, kSslErrLen);
  return Status(Status::TLS_ERROR, buf);
}

TLSSocket::~TLSSocket() {
  if (ssl_ != nullptr) {
    SSL_free(ssl_);
  }
}

Status TLSSocket::connectTLS(const Address& addr, absl::string_view hostName,
                             SSL_CTX* ctx) {
  const Status cs = Socket::connect(addr);
  if (!cs.ok()) {
    return cs;
  }

  ssl_ = SSL_new(ctx);
  if (ssl_ == nullptr) {
    return makeTLSError(ERR_get_error());
  }

  int sslErr = SSL_set_fd(ssl_, fd_);
  if (sslErr != 1) {
    return makeTLSError(sslErr);
  }

  std::string hostNameStr(hostName);
  sslErr = SSL_set_tlsext_host_name(ssl_, hostNameStr.c_str());
  if (sslErr != 1) {
    return makeTLSError(sslErr);
  }

  SSL_set_connect_state(ssl_);
  return Status::kOk;
}

StatusOr<IOStatus> TLSSocket::write(const void* buf, size_t count,
                                    size_t* written) {
  assert(written != nullptr);
  const int s = SSL_write(ssl_, buf, count);
  if (s > 0) {
    *written = s;
    return OK;
  }

  // Man page says that "0" means "failure".
  *written = 0;
  const int sslErr = SSL_get_error(ssl_, s);
  switch (sslErr) {
    case SSL_ERROR_WANT_READ:
      return NEED_READ;
    case SSL_ERROR_WANT_WRITE:
      return NEED_WRITE;
    default:
      return makeTLSError(sslErr);
  }
}

StatusOr<IOStatus> TLSSocket::read(void* buf, size_t count, size_t* readed) {
  const int s = SSL_read(ssl_, buf, count);
  if (s > 0) {
    *readed = s;
    return OK;
  }

  *readed = 0;
  int sentShutdown = (SSL_get_shutdown(ssl_) & SSL_SENT_SHUTDOWN);

  const int sslErr = SSL_get_error(ssl_, s);
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
      return makeTLSError(sslErr);
    default:
      return makeTLSError(sslErr);
  }
}

StatusOr<IOStatus> TLSSocket::close() {
  const int s = SSL_shutdown(ssl_);
  if (s == 1) {
    ::close(fd_);
    return OK;
  }
  if (s == 0) {
    // Docs say that we must call SSL_read here.
    size_t readed;
    return read(NULL, 0, &readed);
  }

  const int sslErr = SSL_get_error(ssl_, s);
  switch (sslErr) {
    case SSL_ERROR_WANT_READ:
      return NEED_READ;
    case SSL_ERROR_WANT_WRITE:
      return NEED_WRITE;
    default:
      return makeTLSError(sslErr);
  }
}

}  // namespace apib
