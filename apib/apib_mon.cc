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

#include "apib/apib_mon.h"

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include <cassert>
#include <functional>
#include <iomanip>
#include <iostream>
#include <sstream>

#include "apib/apib_cpu.h"
#include "apib/apib_lines.h"
#include "apib/apib_util.h"

using std::cerr;
using std::cout;
using std::endl;

namespace apib {

#define LISTEN_BACKLOG 8
#define READ_BUF_LEN 128
#define PROC_BUF_LEN 512

MonServerConnection::MonServerConnection(int fd) : fd_(fd) {}

size_t MonServerConnection::sendBack(const absl::string_view msg) {
  return write(fd_, msg.data(), msg.size());
}

bool MonServerConnection::processCommand(const absl::string_view cmd,
                                         CPUUsage* lastUsage) {
  if (eqcase(cmd, "HELLO")) {
    sendBack("Hi!\n");

  } else if (eqcase(cmd, "CPU")) {
    double usage = cpu_GetInterval(lastUsage);
    std::ostringstream buf;
    buf << std::fixed << std::setprecision(2) << usage << '\n';
    sendBack(buf.str());

  } else if (eqcase(cmd, "MEM")) {
    double usage = cpu_GetMemoryUsage();
    std::ostringstream buf;
    buf << std::fixed << std::setprecision(2) << usage << '\n';
    sendBack(buf.str());

  } else if (eqcase(cmd, "BYE") || eqcase(cmd, "QUIT")) {
    sendBack("BYE\n");
    return true;

  } else {
    sendBack("Invalid command\n");
  }
  return false;
}

void MonServerConnection::socketLoop() {
  bool closeRequested = false;
  CPUUsage lastUsage;

  cpu_GetUsage(&lastUsage);
  LineState line(READ_BUF_LEN);

  while (!closeRequested) {
    int s = line.readFd(fd_);
    if (s <= 0) {
      break;
    }
    while (!closeRequested && line.next()) {
      const auto l = line.line();
      closeRequested = processCommand(l, &lastUsage);
    }
    if (!closeRequested) {
      if (!line.consume()) {
        /* Line too big to fit in buffer -- abort */
        break;
      }
    }
  }

  close(fd_);
  delete this;
}

void MonServer::acceptLoop() { ev_run(loop_, 0); }

void MonServer::acceptOne() {
  const int fd = accept(listenfd_, nullptr, nullptr);
  if (fd <= 0) {
    // This could be because the socket was closed.
    cerr << "Error accepting socket: " << errno << endl;
    return;
  }

  // Explicitly clear the blocking flag, because it might be inherited from
  // the accept FD.
  int flags = fcntl(fd, F_GETFL);
  flags &= ~O_NONBLOCK;
  int err = fcntl(fd, F_SETFL, flags);
  assert(err == 0);

  MonServerConnection* c = new MonServerConnection(fd);
  std::thread ct(std::bind(&MonServerConnection::socketLoop, c));
  ct.detach();
}

// Called by libev when there is something to accept()
static void handleAccept(struct ev_loop* loop, ev_io* io, int revents) {
  auto s = reinterpret_cast<MonServer*>(io->data);
  s->acceptOne();
}

// Called by libev when it's time to stop
static void handleShutdown(struct ev_loop* loop, ev_async* a, int revents) {
  // One reference is the accept thread, and the other is ourself!
  ev_unref(loop);
  ev_unref(loop);
}

int MonServer::start(const std::string& address, int port) {
  int err = cpu_Init();
  if (err != 0) {
    fprintf(stderr, "CPU monitoring not available on this platform\n");
    return -3;
  }

  loop_ = ev_loop_new(EVFLAG_AUTO);

  listenfd_ = socket(AF_INET, SOCK_STREAM, 0);
  if (listenfd_ < 0) {
    cerr << "Can't create socket: " << errno << endl;
    return -1;
  }

  err = fcntl(listenfd_, F_SETFL, O_NONBLOCK);
  if (err != 0) {
    cerr << "Can't set socket mode: " << errno << endl;
    close(listenfd_);
    return -4;
  }

  struct sockaddr_in addr;
  addr.sin_family = AF_INET;
  addr.sin_port = htons(port);
  // Listen on localhost to avoid weird firewall stuff on Macs
  // We may have to revisit if we test on platforms with a different address.
  addr.sin_addr.s_addr = inet_addr(address.c_str());

  err = bind(listenfd_, (const struct sockaddr*)&addr,
             sizeof(struct sockaddr_in));
  if (err != 0) {
    cerr << "Can't bind to port: " << errno << endl;
    return -2;
  }

  err = listen(listenfd_, LISTEN_BACKLOG);
  if (err != 0) {
    cerr << "Can't listen on socket: " << errno << endl;
    close(listenfd_);
    return -3;
  }

  ev_async_init(&shutdownev_, handleShutdown);
  ev_async_start(loop_, &shutdownev_);

  ev_io_init(&listenev_, handleAccept, listenfd_, EV_READ);
  listenev_.data = this;
  ev_io_start(loop_, &listenev_);

  acceptThread_.reset(new std::thread(std::bind(&MonServer::acceptLoop, this)));
  return 0;
}

MonServer::~MonServer() {
  if (loop_ != nullptr) {
    ev_loop_destroy(loop_);
  }
}

int MonServer::port() const {
  struct sockaddr_in addr;
  socklen_t addrlen = sizeof(struct sockaddr_in);

  getsockname(listenfd_, (struct sockaddr*)&addr, &addrlen);
  return ntohs(addr.sin_port);
}

void MonServer::stop() {
  ev_async_send(loop_, &shutdownev_);
  acceptThread_->join();
  close(listenfd_);
}

void MonServer::join() { acceptThread_->join(); }

}  // namespace apib