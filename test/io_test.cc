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

#include <iostream>

#include "apib/apib_iothread.h"
#include "apib/apib_reporting.h"
#include "apib/apib_url.h"
#include "gtest/gtest.h"
#include "test/test_server.h"

using apib::BenchmarkResults;
using apib::IOThread;
using apib::RecordStart;
using apib::RecordStop;
using apib::ReportResults;
using apib::ThreadList;
using apib::URLInfo;

namespace {

static int testServerPort;
static apib::TestServer testServer;

class IOTest : public ::testing::Test {
 protected:
  IOTest() {
    apib::RecordInit("", "");
    testServer.resetStats();
  }
  ~IOTest() {
    // The "url_" family of functions use static data, so reset every time.
    URLInfo::Reset();
    apib::EndReporting();
  }
  apib::ThreadList threads;
};

static void compareReporting() {
  apib::TestServerStats stats = testServer.stats();
  BenchmarkResults results = ReportResults();

  EXPECT_LT(0, results.successfulRequests);
  EXPECT_EQ(0, results.unsuccessfulRequests);
  EXPECT_EQ(0, results.socketErrors);

  EXPECT_EQ(results.successfulRequests, stats.successCount);
  EXPECT_EQ(results.unsuccessfulRequests, stats.errorCount);
}

TEST_F(IOTest, OneThread) {
  char url[128];
  sprintf(url, "http://127.0.0.1:%i/hello", testServerPort);
  URLInfo::InitOne(url);

  IOThread* t = new IOThread();
  threads.push_back(std::unique_ptr<IOThread>(t));
  t->numConnections = 1;
  t->httpVerb = "GET";
  t->verbose = false;

  RecordStart(true, threads);
  t->Start();
  sleep(1);
  t->Stop();
  RecordStop(threads);
  compareReporting();
}

TEST_F(IOTest, OneRequest) {
  char url[128];
  sprintf(url, "http://127.0.0.1:%i/hello", testServerPort);
  URLInfo::InitOne(url);

  IOThread* t = new IOThread();
  threads.push_back(std::unique_ptr<IOThread>(t));
  t->numConnections = 1;
  // t.verbose = 1;
  t->httpVerb = "GET";
  t->keepRunning = -1;

  RecordStart(true, threads);
  t->Start();
  t->Join();
  RecordStop(threads);

  BenchmarkResults results = ReportResults();

  EXPECT_EQ(1, results.successfulRequests);
  EXPECT_EQ(0, results.unsuccessfulRequests);
  EXPECT_EQ(0, results.socketErrors);
}

TEST_F(IOTest, OneThreadNoKeepAlive) {
  char url[128];
  sprintf(url, "http://127.0.0.1:%i/hello", testServerPort);
  URLInfo::InitOne(url);

  IOThread* t = new IOThread();
  threads.push_back(std::unique_ptr<IOThread>(t));
  t->numConnections = 1;
  // t.verbose = 1;
  t->httpVerb = "GET";
  t->noKeepAlive = 1;

  RecordStart(true, threads);
  t->Start();
  sleep(1);
  t->Stop();
  RecordStop(threads);

  compareReporting();
  BenchmarkResults results = ReportResults();
  EXPECT_LT(1, results.connectionsOpened);
  EXPECT_EQ(results.completedRequests, results.connectionsOpened);
}

TEST_F(IOTest, OneThreadThinkTime) {
  char url[128];
  sprintf(url, "http://127.0.0.1:%i/hello", testServerPort);
  URLInfo::InitOne(url);

  IOThread* t = new IOThread();
  threads.push_back(std::unique_ptr<IOThread>(t));
  t->numConnections = 1;
  // t.verbose = 1;
  t->httpVerb = "GET";
  t->thinkTime = 100;

  RecordStart(true, threads);
  t->Start();
  sleep(1);
  t->Stop();
  RecordStop(threads);

  compareReporting();
}

TEST_F(IOTest, OneThreadThinkTimeNoKeepAlive) {
  char url[128];
  sprintf(url, "http://127.0.0.1:%i/hello", testServerPort);
  URLInfo::InitOne(url);

  IOThread* t = new IOThread();
  threads.push_back(std::unique_ptr<IOThread>(t));
  t->numConnections = 1;
  // t->verbose = 1;
  t->httpVerb = "GET";
  t->thinkTime = 100;
  t->noKeepAlive = 1;

  RecordStart(true, threads);
  t->Start();
  sleep(1);
  t->Stop();
  RecordStop(threads);

  compareReporting();
}

TEST_F(IOTest, OneThreadLarge) {
  char url[128];
  sprintf(url, "http://127.0.0.1:%i/data?size=4000", testServerPort);
  URLInfo::InitOne(url);

  IOThread* t = new IOThread();
  threads.push_back(std::unique_ptr<IOThread>(t));
  t->numConnections = 1;
  // t->verbose = 1;
  t->httpVerb = "GET";

  RecordStart(true, threads);
  t->Start();
  sleep(1);
  t->Stop();
  RecordStop(threads);

  compareReporting();
}

TEST_F(IOTest, MoreConnections) {
  char url[128];
  sprintf(url, "http://127.0.0.1:%i/hello", testServerPort);
  URLInfo::InitOne(url);

  IOThread* t = new IOThread();
  threads.push_back(std::unique_ptr<IOThread>(t));
  t->numConnections = 10;
  // t->verbose = 1;
  t->httpVerb = "GET";

  RecordStart(true, threads);
  t->Start();
  sleep(1);
  t->Stop();
  RecordStop(threads);

  compareReporting();
}

TEST_F(IOTest, ResizeCommand) {
  char url[128];
  sprintf(url, "http://127.0.0.1:%i/hello", testServerPort);
  URLInfo::InitOne(url);

  IOThread* t = new IOThread();
  threads.push_back(std::unique_ptr<IOThread>(t));
  t->numConnections = 1;
  // t->verbose = 1;
  t->httpVerb = "GET";

  RecordStart(true, threads);
  t->Start();
  usleep(250000);
  t->SetNumConnections(5);
  usleep(250000);
  t->SetNumConnections(2);
  t->SetNumConnections(3);
  t->SetNumConnections(1);
  usleep(250000);
  t->Stop();
  RecordStop(threads);

  compareReporting();
}

TEST_F(IOTest, ResizeFromZero) {
  char url[128];
  sprintf(url, "http://127.0.0.1:%i/hello", testServerPort);
  URLInfo::InitOne(url);

  IOThread* t = new IOThread();
  threads.push_back(std::unique_ptr<IOThread>(t));
  t->numConnections = 0;
  // t->verbose = true;
  t->httpVerb = "GET";

  RecordStart(true, threads);
  t->Start();
  usleep(250000);
  t->SetNumConnections(5);
  usleep(250000);
  t->Stop();
  RecordStop(threads);

  compareReporting();
}

#define POST_LEN 3000

TEST_F(IOTest, OneThreadBigPost) {
  char url[128];
  sprintf(url, "http://127.0.0.1:%i/echo", testServerPort);
  URLInfo::InitOne(url);

  IOThread* t = new IOThread();
  threads.push_back(std::unique_ptr<IOThread>(t));
  t->numConnections = 1;
  // t->verbose = 1;
  t->httpVerb = "POST";
  for (int p = 0; p < POST_LEN; p += 10) {
    t->sendData.append("abcdefghij");
  }

  RecordStart(true, threads);
  t->Start();
  sleep(1);
  t->Stop();
  RecordStop(threads);

  compareReporting();
}

TEST_F(IOTest, OneThreadHeaders) {
  char url[128];
  sprintf(url, "http://127.0.0.1:%i/hello", testServerPort);
  URLInfo::InitOne(url);

  IOThread* t = new IOThread();
  threads.push_back(std::unique_ptr<IOThread>(t));
  t->numConnections = 1;
  // t->verbose = 1;
  t->httpVerb = "GET";
  t->headers = new std::vector<std::string>();
  t->headers->push_back("Authorization: Basic dGVzdDp2ZXJ5dmVyeXNlY3JldA==");

  RecordStart(true, threads);
  t->Start();
  sleep(1);
  t->Stop();
  RecordStop(threads);

  compareReporting();
  delete t->headers;
}

TEST_F(IOTest, IP6Address) {
  // Start and stop a separate server here on a different address and port
  apib::TestServer testServer6;
  int err = testServer6.start("::1", 0, "", "");
  if (err != 0) {
    GTEST_SKIP() << "Can't listen on IP6";
  }

  char url[128];
  sprintf(url, "http://[::1]:%i/hello", testServer6.port());
  URLInfo::InitOne(url);

  IOThread* t = new IOThread();
  threads.push_back(std::unique_ptr<IOThread>(t));
  t->numConnections = 1;
  t->httpVerb = "GET";

  RecordStart(true, threads);
  t->Start();
  sleep(1);
  t->Stop();
  RecordStop(threads);
  BenchmarkResults results = ReportResults();

  EXPECT_LT(0, results.successfulRequests);
  EXPECT_EQ(0, results.unsuccessfulRequests);
  EXPECT_EQ(0, results.socketErrors);

  testServer6.stop();
}

}  // namespace

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);

  int err = testServer.start("127.0.0.1", 0, "", "");
  if (err != 0) {
    fprintf(stderr, "Can't start test server: %i\n", err);
    return 2;
  }
  testServerPort = testServer.port();

  int r = RUN_ALL_TESTS();

  testServer.stop();

  return r;
}
