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
#include <stdio.h>
#include <sys/socket.h>
#include <sys/types.h>

#include "gtest/gtest.h"
#include "src/apib_cpu.h"
#include "src/apib_lines.h"
#include "src/apib_mon.h"
#include "src/apib_util.h"

static int ServerPort;

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
  printf("Memory is \"%lf\"", mem);
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
  printf("CPU %s\n", buf);
  EXPECT_NE(buf, bufEnd);
  EXPECT_LE(0.0, c);
  printf("CPU is \"%lf\"", c);
}

TEST_F(MonServerTest, Multi) {
  size_t ws = write(fd, "CPU\nCPU\nCPU\nBYE\n", 16);
  ASSERT_EQ(16, ws);

  LineState line;
  char buf[256];
  linep_Start(&line, buf, 256, 0);

  int rc;
  int cmd = 0;
  do {
    rc = linep_ReadFd(&line, fd);
    if (rc > 0) {
      while (linep_NextLine(&line)) {
        char* l = linep_GetLine(&line);
        if (cmd < 3) {
          char* bufEnd;
          double c = strtod(l, &bufEnd);
          printf("CPU = %s\n", l);
          EXPECT_LE(0.0, c);
          EXPECT_NE(l, bufEnd);
        } else {
          EXPECT_STREQ("BYE", l);
        }
        cmd++;
      }
    }
  } while (rc > 0);
  EXPECT_EQ(0, rc);
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);

  int err = cpu_Init();
  if (err != 0) {
    fprintf(stderr,
            "Skipping monitoring tests: CPU monitoring not available on "
            "platform\n");
    return 0;
  }

  MonServer mon;
  err = mon_StartServer(&mon, "127.0.0.1", 0);
  if (err != 0) {
    fprintf(stderr, "Can't start server\n");
    return 2;
  }

  ServerPort = mon_GetPort(&mon);
  printf("Mon server running on %i\n", ServerPort);

  int ret = RUN_ALL_TESTS();

  mon_StopServer(&mon);
  return ret;
}
