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

#include <memory>

#include "gtest/gtest.h"
#include "apib/apib_iothread.h"
#include "apib/apib_reporting.h"

using apib::BenchmarkIntervalResults;
using apib::BenchmarkResults;
using apib::IOThread;
using apib::RecordConnectionOpen;
using apib::RecordSocketError;
using apib::RecordStart;
using apib::RecordStop;
using apib::ReportIntervalResults;
using apib::ReportResults;
using apib::ThreadList;

namespace {

class Reporting : public ::testing::Test {
 protected:
  Reporting() { apib::RecordInit("", ""); }
  ~Reporting() { apib::EndReporting(); }
  ThreadList threads;
};

TEST_F(Reporting, ReportingZero) {
  // "threads" list will be empty here.
  RecordStart(true, threads);
  RecordStop(threads);
  BenchmarkResults r = ReportResults();
  EXPECT_EQ(0, r.completedRequests);
  EXPECT_EQ(0, r.successfulRequests);
  EXPECT_EQ(0, r.unsuccessfulRequests);
  EXPECT_EQ(0, r.socketErrors);
  EXPECT_EQ(0, r.connectionsOpened);
  EXPECT_EQ(0, r.totalBytesSent);
  EXPECT_EQ(0, r.totalBytesReceived);
}

TEST_F(Reporting, ReportingCount) {
  threads.push_back(std::unique_ptr<IOThread>(new IOThread()));
  RecordStart(true, threads);
  RecordConnectionOpen();
  threads[0]->recordResult(200, 120000000);
  threads[0]->recordResult(201, 110000000);
  threads[0]->recordResult(204, 100000000);
  threads[0]->recordResult(403, 110000000);
  threads[0]->recordResult(401, 100000000);
  threads[0]->recordResult(500, 100000000);
  threads[0]->recordRead(100);
  threads[0]->recordWrite(100);
  threads[0]->recordRead(100);
  RecordSocketError();
  RecordConnectionOpen();
  RecordStop(threads);

  BenchmarkResults r = ReportResults();

  EXPECT_EQ(6, r.completedRequests);
  EXPECT_EQ(3, r.successfulRequests);
  EXPECT_EQ(3, r.unsuccessfulRequests);
  EXPECT_EQ(1, r.socketErrors);
  EXPECT_EQ(2, r.connectionsOpened);
  EXPECT_EQ(100, r.totalBytesSent);
  EXPECT_EQ(200, r.totalBytesReceived);
  EXPECT_EQ(100.0, r.latencies[0]);
  EXPECT_EQ(120.0, r.latencies[100]);
}

TEST_F(Reporting, ReportingInterval) {
  threads.push_back(std::unique_ptr<IOThread>(new IOThread()));
  RecordStart(true, threads);
  RecordConnectionOpen();
  threads[0]->recordResult(200, 120000000);
  threads[0]->recordResult(201, 120000000);
  threads[0]->recordResult(400, 120000000);

  BenchmarkIntervalResults ri = ReportIntervalResults(threads);
  EXPECT_EQ(2, ri.successfulRequests);
  EXPECT_LT(0.0, ri.averageThroughput);

  threads[0]->recordResult(204, 120000000);
  threads[0]->recordResult(403, 120000000);
  threads[0]->recordResult(401, 120000000);
  threads[0]->recordResult(500, 120000000);
  threads[0]->recordResult(200, 120000000);

  ri = ReportIntervalResults(threads);
  EXPECT_EQ(2, ri.successfulRequests);
  EXPECT_LT(0.0, ri.averageThroughput);

  RecordStop(threads);
  BenchmarkResults r = ReportResults();

  EXPECT_EQ(8, r.completedRequests);
  EXPECT_EQ(4, r.successfulRequests);
  EXPECT_EQ(4, r.unsuccessfulRequests);
  EXPECT_EQ(0, r.socketErrors);
  EXPECT_EQ(1, r.connectionsOpened);
  EXPECT_EQ(0, r.totalBytesSent);
  EXPECT_EQ(0, r.totalBytesReceived);
}

}  // namespace