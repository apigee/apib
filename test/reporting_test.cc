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
#include "src/apib_reporting.h"

using apib::BenchmarkResults;
using apib::BenchmarkIntervalResults;
using apib::RecordByteCounts;
using apib::RecordConnectionOpen;
using apib::RecordSocketError;
using apib::RecordStart;
using apib::RecordStop;
using apib::RecordResult;
using apib::ReportResults;
using apib::ReportIntervalResults;

namespace {

class Reporting : public ::testing::Test {
 protected:
  Reporting() { apib::RecordInit("", ""); }
  ~Reporting() { apib::EndReporting(); }
};

TEST_F(Reporting, ReportingZero) {
  RecordStart(true);
  RecordStop();
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
  RecordStart(1);
  RecordConnectionOpen();
  RecordResult(200, 100000000);
  RecordResult(201, 110000000);
  RecordResult(204, 120000000);
  RecordResult(403, 100000000);
  RecordResult(401, 100000000);
  RecordResult(500, 100000000);
  RecordSocketError();
  RecordConnectionOpen();
  RecordByteCounts(100, 200);
  RecordStop();

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
  RecordStart(1);
  RecordConnectionOpen();
  RecordResult(200, 100000000);
  RecordResult(201, 100000000);
  RecordResult(400, 100000000);

  BenchmarkIntervalResults ri = ReportIntervalResults();
  EXPECT_EQ(2, ri.successfulRequests);
  EXPECT_LT(0.0, ri.averageThroughput);

  RecordResult(204, 100000000);
  RecordResult(403, 100000000);
  RecordResult(401, 100000000);
  RecordResult(500, 100000000);
  RecordResult(200, 100000000);

  ri = ReportIntervalResults();
  EXPECT_EQ(2, ri.successfulRequests);
  EXPECT_LT(0.0, ri.averageThroughput);

  RecordStop();
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