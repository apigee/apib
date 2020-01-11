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

#include "apib/socket.h"

#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <unistd.h>

#include <cassert>

namespace apib {

Socket::~Socket() {
  if (fd_ != 0) {
    ::close(fd_);
  }
}

Status Socket::connect(const Address& addr) {
  fd_ = socket(addr.family(), SOCK_STREAM, 0);
  if (fd_ <= 0) {
    return Status(Status::SOCKET_ERROR, errno);
  }

  socklen_t addrlen = 0;
  int yes = 1;
  // Set NODELAY to minimize latency and because we are not processing
  // keystrokes
  int err = setsockopt(fd_, IPPROTO_TCP, TCP_NODELAY, &yes, sizeof(int));
  if (err != 0) {
    goto fail;
  }
  // Set REUSEADDR for convenience when running lots of tests
  err = setsockopt(fd_, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int));
  if (err != 0) {
    goto fail;
  }

  struct linger linger;
  linger.l_onoff = 0;
  linger.l_linger = 0;
  // Disable LINGER so that we don't run out of sockets when testing with
  // keep-alive disabled. (We'll still need kernel settings often too.)
  err = setsockopt(fd_, SOL_SOCKET, SO_LINGER, &linger, sizeof(struct linger));
  if (err != 0) {
    goto fail;
  }

  // The socket must be non-blocking or the rest of the logic doesn't work.
  err = fcntl(fd_, F_SETFL, O_NONBLOCK);
  if (err != 0) {
    goto fail;
  }

  struct sockaddr_storage netaddr;
  addrlen = addr.get(&netaddr);
  err = ::connect(fd_, (struct sockaddr*)&netaddr, addrlen);
  if ((err != 0) && (errno != EINPROGRESS)) {
    goto fail;
  }
  return Status::kOk;

fail:
  const Status failStat = Status(Status::SOCKET_ERROR, errno);
  ::close(fd_);
  return failStat;
}

StatusOr<IOStatus> Socket::write(const void* buf, size_t count,
                                 size_t* written) {
  assert(written != nullptr);
  const auto ws = ::write(fd_, buf, count);
  if (ws < 0) {
    if ((errno == EAGAIN) || (errno == EWOULDBLOCK)) {
      return IOStatus::NEED_WRITE;
    }
    return Status(Status::SOCKET_ERROR, errno);
  }
  *written = ws;
  return IOStatus::OK;
}

StatusOr<IOStatus> Socket::read(void* buf, size_t count, size_t* readed) {
  assert(readed != nullptr);
  const auto rs = ::read(fd_, buf, count);
  if (rs < 0) {
    if ((errno == EAGAIN) || (errno == EWOULDBLOCK)) {
      return IOStatus::NEED_READ;
    }
    return Status(Status::SOCKET_ERROR, errno);
  }
  *readed = rs;
  return IOStatus::OK;
}

StatusOr<IOStatus> Socket::close() {
  const auto cs = ::close(fd_);
  fd_ = 0;
  if (cs != 0) {
    return Status(Status::SOCKET_ERROR, errno);
  }
  return IOStatus::OK;
}

}  // namespace apib