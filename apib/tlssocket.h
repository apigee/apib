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

#ifndef APIB_TLS_SOCKET_H
#define APIB_TLS_SOCKET_H

#include "absl/strings/string_view.h"
#include "apib/socket.h"
#include "openssl/ssl.h"

namespace apib {

class TLSSocket : public Socket {
 public:
  TLSSocket() {}
  TLSSocket(const TLSSocket&) = delete;
  TLSSocket& operator=(const Socket&) = delete;
  virtual ~TLSSocket();

  Status connectTLS(const Address& addr, absl::string_view hostName,
                    SSL_CTX* ctx);
  StatusOr<IOStatus> write(const void* buf, size_t count,
                           size_t* written) override;
  StatusOr<IOStatus> read(void* buf, size_t count, size_t* readed) override;
  StatusOr<IOStatus> close() override;

 private:
  SSL* ssl_ = nullptr;
};

}  // namespace apib

#endif
