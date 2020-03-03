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

#ifndef APIB_EVENTLOOP_H
#define APIB_EVENTLOOP_H

#include <functional>
#include <string>

#include "ev.h"

namespace apib {

class EventLoop;

typedef std::function<void()> TimerCb;

class Timer {
 public:
  Timer() : loop_(nullptr) {}
  Timer(EventLoop* l) : loop_(l) {}
  Timer(const Timer&) = delete;
  Timer& operator = (const Timer&) = delete;

  void setLoop(EventLoop* l) { loop_ = l; }

  void start(EventLoop* l, double delaySeconds, TimerCb cb);

 private:
  EventLoop* loop_;
  struct ev_timer timer_;
};

/*
 * This is an abstraction for the libev event loop that makes things a bit easier.
 */
class EventLoop {
 public:
  EventLoop() : loop_(nullptr) {}
  EventLoop(const EventLoop&) = delete;
  ~EventLoop();
  EventLoop& operator = (const EventLoop&) = delete;

  struct ev_loop* loop() const { return loop_; }

  // One-time init. It's undefined to not call this.
  void init(int flags = EVFLAG_AUTO);

  // Manually manage reference counts, for advanced use cases
  void ref() { 
    ev_ref(loop_);
  }

  void unref() {
    ev_unref(loop_);
  }

  // Start running the loop, exiting when all references are zero
  int run() {
    return ev_run(loop_, 0);
  }
  void breakAll() {
    ev_break(loop_, EVBREAK_ALL);
  }

  // Return names of the backends supported, for debugging.
  std::string backends();
  
 private:
  struct ev_loop* loop_;
};

}

#endif