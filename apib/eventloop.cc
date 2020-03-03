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

#include <cassert>
#include <vector>

#include "absl/strings/str_join.h"
#include "apib/eventloop.h"

namespace apib {

static void timerDone(struct ev_loop* loop, ev_timer* t, int revents) {
  TimerCb cb = *(static_cast<TimerCb*>(t->data));
  cb();
}

void Timer::start(EventLoop* l, double delaySeconds, TimerCb cb) {
  loop_ = l;
  ev_timer_init(&timer_, timerDone, delaySeconds, 0);
  timer_.data = &cb;
  ev_timer_start(loop_->loop(), &timer_);
}

EventLoop::~EventLoop() {
  if (loop_ != nullptr) {
    ev_loop_destroy(loop_);
  }
}

void EventLoop::init(int flags) {
  assert(loop_ == nullptr);
  loop_ = ev_loop_new(flags);
}

std::string EventLoop::backends() {
  const int b = ev_backend(loop_);
  std::vector<std::string> formats;
  if (b & EVBACKEND_SELECT) {
    formats.push_back("select");
  }
  if (b & EVBACKEND_POLL) {
    formats.push_back("poll");
  }
  if (b & EVBACKEND_EPOLL) {
    formats.push_back("epoll");
  }
  if (b & EVBACKEND_LINUXAIO) {
    formats.push_back("linux AIO");
  }
  if (b & EVBACKEND_KQUEUE) {
    formats.push_back("kqueue");
  }
  if (b & EVBACKEND_DEVPOLL) {
    formats.push_back("/dev/poll");
  }
  if (b & EVBACKEND_PORT) {
    formats.push_back("Solaris event port");
  }
  return absl::StrJoin(formats, ", ");
}

}