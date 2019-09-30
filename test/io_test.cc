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
#include "src/apib_iothread.h"
#include "src/apib_message.h"
#include "src/apib_reporting.h"
#include "src/apib_url.h"
#include "test/test_server.h"

using apib::BenchmarkResults;
using apib::IOThread;
using apib::ReportResults;
using apib::RecordStop;
using apib::URLInfo;

namespace {

static int testServerPort;
static TestServer* testServer;

class IOTest : public ::testing::Test {
 protected:
  IOTest() {
    apib::RecordInit("", "");
    apib::RecordStart(true);
    testserver_ResetStats(testServer);
  }
  ~IOTest() {
    // The "url_" family of functions use static data, so reset every time.
    URLInfo::Reset();
    apib::EndReporting();
  }
};

static void compareReporting() {
  TestServerStats stats;
  testserver_GetStats(testServer, &stats);
  BenchmarkResults results = ReportResults();

  EXPECT_LT(0, results.successfulRequests);
  EXPECT_EQ(0, results.unsuccessfulRequests);
  EXPECT_EQ(0, results.socketErrors);

  EXPECT_EQ(results.successfulRequests, stats.successCount);
  EXPECT_EQ(results.unsuccessfulRequests, stats.errorCount);
  EXPECT_EQ(results.socketErrors, stats.socketErrorCount);
  EXPECT_EQ(results.connectionsOpened, stats.connectionCount);
}

TEST_F(IOTest, OneThread) {
  char url[128];
  sprintf(url, "http://localhost:%i/hello", testServerPort);
  URLInfo::InitOne(url);

  IOThread t;
  t.numConnections = 1;
  t.httpVerb = "GET";

  t.Start();
  sleep(1);
  t.Stop();
  RecordStop();

  compareReporting();
}

TEST_F(IOTest, OneRequest) {
  char url[128];
  sprintf(url, "http://localhost:%i/hello", testServerPort);
  URLInfo::InitOne(url);

  IOThread t;
  t.numConnections = 1;
  // t.verbose = 1;
  t.httpVerb = "GET";
  t.keepRunning = -1;

  t.Start();
  t.Join();
  RecordStop();

  BenchmarkResults results = ReportResults();

  EXPECT_EQ(1, results.successfulRequests);
  EXPECT_EQ(0, results.unsuccessfulRequests);
  EXPECT_EQ(0, results.socketErrors);
}

TEST_F(IOTest, OneThreadNoKeepAlive) {
  char url[128];
  sprintf(url, "http://localhost:%i/hello", testServerPort);
  URLInfo::InitOne(url);

  IOThread t;
  t.numConnections = 1;
  // t.verbose = 1;
  t.httpVerb = "GET";
  t.noKeepAlive = 1;

  t.Start();
  sleep(1);
  t.Stop();
  RecordStop();

  compareReporting();
  BenchmarkResults results = ReportResults();
  EXPECT_LT(1, results.connectionsOpened);
  EXPECT_EQ(results.completedRequests, results.connectionsOpened);
}

TEST_F(IOTest, OneThreadThinkTime) {
  char url[128];
  sprintf(url, "http://localhost:%i/hello", testServerPort);
  URLInfo::InitOne(url);

  IOThread t;
  t.numConnections = 1;
  // t.verbose = 1;
  t.httpVerb = "GET";
  t.thinkTime = 100;

  t.Start();
  sleep(1);
  t.Stop();
  RecordStop();

  compareReporting();
}

TEST_F(IOTest, OneThreadThinkTimeNoKeepAlive) {
  char url[128];
  sprintf(url, "http://localhost:%i/hello", testServerPort);
  URLInfo::InitOne(url);

  IOThread t;
  t.numConnections = 1;
  // t.verbose = 1;
  t.httpVerb = "GET";
  t.thinkTime = 100;
  t.noKeepAlive = 1;

  t.Start();
  sleep(1);
  t.Stop();
  RecordStop();

  compareReporting();
}

TEST_F(IOTest, OneThreadLarge) {
  char url[128];
  sprintf(url, "http://localhost:%i/data?size=4000", testServerPort);
  URLInfo::InitOne(url);

  IOThread t;
  t.numConnections = 1;
  // t.verbose = 1;
  t.httpVerb = "GET";

  t.Start();
  sleep(1);
  t.Stop();
  RecordStop();

  compareReporting();
}

TEST_F(IOTest, MoreConnections) {
  char url[128];
  sprintf(url, "http://localhost:%i/hello", testServerPort);
  URLInfo::InitOne(url);

  IOThread t;
  t.numConnections = 10;
  // t.verbose = 1;
  t.httpVerb = "GET";

  t.Start();
  sleep(1);
  t.Stop();
  RecordStop();

  compareReporting();
}

TEST_F(IOTest, ResizeCommand) {
  char url[128];
  sprintf(url, "http://localhost:%i/hello", testServerPort);
  URLInfo::InitOne(url);

  IOThread t;
  t.numConnections = 1;
  // t.verbose = 1;
  t.httpVerb = "GET";

  t.Start();
  usleep(250000);
  t.SetNumConnections(5);
  usleep(250000);
  t.SetNumConnections(2);
  t.SetNumConnections(3);
  t.SetNumConnections(1);
  usleep(250000);
  t.Stop();
  RecordStop();

  compareReporting();
}

TEST_F(IOTest, ResizeFromZero) {
  char url[128];
  sprintf(url, "http://localhost:%i/hello", testServerPort);
  URLInfo::InitOne(url);

  IOThread t;
  t.numConnections = 0;
  // t.verbose = true;
  t.httpVerb = "GET";

  t.Start();
  usleep(250000);
  t.SetNumConnections(5);
  usleep(250000);
  t.Stop();
  RecordStop();

  compareReporting();
}

#define POST_LEN 3000

TEST_F(IOTest, OneThreadBigPost) {
  char url[128];
  sprintf(url, "http://localhost:%i/echo", testServerPort);
  URLInfo::InitOne(url);

  IOThread t;
  t.numConnections = 1;
  // t.verbose = 1;
  t.httpVerb = "POST";
  for (int p = 0; p < POST_LEN; p += 10) {
    t.sendData.append("abcdefghij");
  }

  t.Start();
  sleep(1);
  t.Stop();
  RecordStop();

  compareReporting();
}

TEST_F(IOTest, OneThreadHeaders) {
  char url[128];
  sprintf(url, "http://localhost:%i/hello", testServerPort);
  URLInfo::InitOne(url);

  IOThread t;
  t.numConnections = 1;
  // t.verbose = 1;
  t.httpVerb = "GET";
  t.headers = new std::vector<std::string>();
  t.headers->push_back("Authorization: Basic dGVzdDp2ZXJ5dmVyeXNlY3JldA==");

  t.Start();
  sleep(1);
  t.Stop();
  RecordStop();

  compareReporting();
  delete t.headers;
}

}  // namespace

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);

  testServer = (TestServer*)malloc(sizeof(TestServer));
  int err = testserver_Start(testServer, "127.0.0.1", 0, NULL, NULL);
  if (err != 0) {
    fprintf(stderr, "Can't start test server: %i\n", err);
    return 2;
  }
  testServerPort = testserver_GetPort(testServer);

  int r = RUN_ALL_TESTS();

  testserver_Stop(testServer);
  // testserver_Join(&svr);
  free(testServer);

  return r;
}
