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

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>

#include <cstdio>
#include <cstring>
#include <iostream>

#include "absl/strings/numbers.h"
#include "gtest/gtest.h"
#include "apib/apib_cpu.h"
#include "apib/apib_lines.h"
#include "apib/apib_mon.h"
#include "apib/apib_util.h"

using std::cout;
using std::endl;

static int ServerPort;

namespace {

class MonServerTest : public ::testing::Test {
 protected:
  int fd;

  MonServerTest() {
    fd = socket(AF_INET, SOCK_STREAM, 0);
    assert(fd > 0);

    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(ServerPort);
    addr.sin_addr.s_addr = inet_addr("127.0.0.1");

    int err = connect(fd, (const sockaddr*)&addr, sizeof(struct sockaddr_in));
    mandatoryAssert(err == 0);
  }

  ~MonServerTest() { close(fd); }
};

TEST_F(MonServerTest, Hello) {
  size_t ws = write(fd, "HELLO\n", 6);
  ASSERT_EQ(6, ws);

  char buf[32];
  ssize_t rs = read(fd, buf, 32);
  EXPECT_LT(0, rs);
  EXPECT_GE(32, rs);
  EXPECT_EQ(0, strncmp(buf, "Hi!\n", 4));
}

TEST_F(MonServerTest, Mem) {
  size_t ws = write(fd, "MEM\n", 4);
  ASSERT_EQ(4, ws);

  char buf[32];
  ssize_t rs = read(fd, buf, 32);
  EXPECT_LT(0, rs);
  EXPECT_GE(32, rs);

  buf[rs] = 0;
  char* bufEnd;
  double mem = strtod(buf, &bufEnd);
  EXPECT_NE(buf, bufEnd);
  EXPECT_LT(0.0, mem);
  cout << "Memory is " << mem << endl;
}

TEST_F(MonServerTest, CPU) {
  size_t ws = write(fd, "CPU\n", 4);
  ASSERT_EQ(4, ws);

  char buf[32];
  ssize_t rs = read(fd, buf, 32);
  EXPECT_LT(0, rs);
  EXPECT_GE(32, rs);

  buf[rs] = 0;
  char* bufEnd;
  double c = strtod(buf, &bufEnd);
  cout << "CPU " << buf << endl;
  EXPECT_NE(buf, bufEnd);
  EXPECT_LE(0.0, c);
  cout << "CPU is " << c << endl;
}

TEST_F(MonServerTest, Multi) {
  size_t ws = write(fd, "CPU\nCPU\nCPU\nBYE\n", 16);
  ASSERT_EQ(16, ws);

  apib::LineState line(256);

  int rc;
  int cmd = 0;
  do {
    rc = line.readFd(fd);
    if (rc > 0) {
      while (line.next()) {
        const auto l = line.line();
        if (cmd < 3) {
          double c;
          EXPECT_TRUE(absl::SimpleAtod(l, &c));
          cout << "CPU = " << l << endl;
          EXPECT_LE(0.0, c);
        } else {
          EXPECT_EQ("BYE", l);
        }
        cmd++;
      }
    }
  } while (rc > 0);
  EXPECT_EQ(0, rc);
}

}  // namespace

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);

  int err = apib::cpu_Init();
  if (err != 0) {
    fprintf(stderr,
            "Skipping monitoring tests: CPU monitoring not available on "
            "platform\n");
    return 0;
  }

  apib::MonServer mon;
  err = mon.start("127.0.0.1", 0);
  if (err != 0) {
    fprintf(stderr, "Can't start server\n");
    return 2;
  }

  ServerPort = mon.port();
  printf("Mon server running on %i\n", ServerPort);

  int ret = RUN_ALL_TESTS();

  mon.stop();
  return ret;
}
