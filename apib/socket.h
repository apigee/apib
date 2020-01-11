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

#ifndef APIB_SOCKET_H
#define APIB_SOCKET_H

#include "apib/addresses.h"
#include "apib/status.h"

namespace apib {

typedef enum { OK, NEED_READ, NEED_WRITE, FEOF } IOStatus;

/*
 * This is an abstraction for a socket. A subclass supports TLS.
 * Although the destructor will free storage (used by SSL), the
 * user must still call close to close everything.
 * The socket will not be actually useful until "connect" is called.
 * Sockets created with this library are always non-blocking.
 */
class Socket {
 public:
  Socket() :fd_(0) {}
  Socket(const Socket&) = delete;
  Socket& operator=(const Socket&) = delete;
  virtual ~Socket();

  int fd() const { return fd_; }

  Status connect(const Address& addr);
  virtual StatusOr<IOStatus> write(const void* buf, size_t count,
                                   size_t* written);
  virtual StatusOr<IOStatus> read(void* buf, size_t count, size_t* readed);
  virtual StatusOr<IOStatus> close();

 protected:
  int fd_;
};

}  // namespace apib

#endif