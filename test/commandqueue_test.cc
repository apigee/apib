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

#include "gtest/gtest.h"
#include "src/apib_commandqueue.h"

using apib::Command;
using apib::CommandQueue;

namespace {

TEST(CommandQueue, Basic) {
  CommandQueue queue;
  Command c1;
  c1.cmd = apib::SET_CONNECTIONS;
  c1.newNumConnections = 1;
  queue.Add(c1);

  Command ret;
  ASSERT_TRUE(queue.Pop(&ret));
  EXPECT_EQ(ret.cmd, apib::SET_CONNECTIONS);
  EXPECT_EQ(1, ret.newNumConnections);
  EXPECT_FALSE(queue.Pop(&ret));
}

TEST(CommandQueue, Larger) {
  CommandQueue queue;
  Command c1;
  c1.cmd = apib::SET_CONNECTIONS;
  c1.newNumConnections = 1;
  queue.Add(c1);
  Command c2;
  c2.cmd = apib::SET_CONNECTIONS;
  c2.newNumConnections = 10;
  queue.Add(c2);
  Command c3;
  c3.cmd = apib::STOP;
  c3.stopTimeoutSecs = 100;
  queue.Add(c3);

  Command ret;
  ASSERT_TRUE(queue.Pop(&ret));
  EXPECT_EQ(apib::SET_CONNECTIONS, ret.cmd);
  EXPECT_EQ(1, ret.newNumConnections);
  ASSERT_TRUE(queue.Pop(&ret));
  EXPECT_EQ(apib::SET_CONNECTIONS, ret.cmd);
  EXPECT_EQ(10, ret.newNumConnections);
  ASSERT_TRUE(queue.Pop(&ret));
  EXPECT_EQ(apib::STOP, ret.cmd);
  EXPECT_EQ(100, ret.stopTimeoutSecs);
  EXPECT_FALSE(queue.Pop(&ret));
}

}  // namespace
