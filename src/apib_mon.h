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

#ifndef APIB_MON_H
#define APIB_MON_H

#include <memory>
#include <string>
#include <thread>

#include "absl/strings/string_view.h"
#include "ev.h"
#include "src/apib_cpu.h"

namespace apib {

class MonServer {
 public:
  ~MonServer();
  int start(const std::string& address, int port);
  int port() const;
  void stop();
  void join();
  void acceptOne();

 private:
  void acceptLoop();

  int listenfd_;
  std::unique_ptr<std::thread> acceptThread_;
  struct ev_loop* loop_ = nullptr;
  ev_io listenev_;
  ev_async shutdownev_;
};

class MonServerConnection {
 public:
  MonServerConnection(int fd);
  void socketLoop();
  bool processCommand(const absl::string_view msg, CPUUsage* lastUsage);
  size_t sendBack(const absl::string_view msg);

 private:
  int fd_;
};

}  // namespace apib

#endif  // APIB_MON_H