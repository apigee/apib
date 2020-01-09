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

#ifndef APIB_COMMANDQ_H
#define APIB_COMMANDQ_H

#include <deque>
#include <mutex>

namespace apib {

typedef enum { STOP, SET_CONNECTIONS } ThreadCmd;

// This is used to send instructions to the thread from outside.
class Command {
 public:
  ThreadCmd cmd;
  int newNumConnections;
  int stopTimeoutSecs;
};

// This is a generic thread-safe queue for commands.
class CommandQueue {
 public:
  void Add(Command cmd);
  // Atomically, either:
  //    return false
  //    or return true and copy the first element to "dest"
  bool Pop(Command* dest);

 private:
  std::deque<Command> commands_;
  std::mutex lock_;
};

}  // namespace apib

#endif  // APIB_COMMANDQ_H