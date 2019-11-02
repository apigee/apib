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

#ifndef APIB_REPORTING_H
#define APIB_REPORTING_H

#include <cstdint>
#include <memory>
#include <ostream>
#include <string>
#include <vector>

#include "src/apib_iothread.h"

namespace apib {

// Per-thread counters. We swap these in and out of IOThreads so that we can
// efficiently count with a minimum of global synchronization
class Counters {
 public:
  int_fast32_t successfulRequests = 0LL;
  int_fast32_t failedRequests = 0LL;
  int_fast64_t bytesRead = 0LL;
  int_fast64_t bytesWritten = 0LL;
  std::vector<int_fast64_t> latencies;
};

class BenchmarkResults {
 public:
  int32_t completedRequests;
  int32_t successfulRequests;
  int32_t unsuccessfulRequests;
  int32_t socketErrors;
  int32_t connectionsOpened;
  int64_t totalBytesSent;
  int64_t totalBytesReceived;

  // Consolidated times in seconds
  double elapsedTime;

  // Consolidated latencies in milliseconds
  double averageLatency;
  double latencyStdDev;
  double latencies[101];

  // Throughput in requests / second
  double averageThroughput;

  // Megabits / second
  double averageSendBandwidth;
  double averageReceiveBandwidth;
};

class BenchmarkIntervalResults {
 public:
  int32_t successfulRequests;
  // In seconds
  double elapsedTime;
  double intervalTime;
  // In tps, for this interval
  double averageThroughput;
};

// One time initialization
extern void RecordInit(const std::string& monitorHost,
                       const std::string& monitor2Host);

// Start a reporting run
extern void RecordStart(bool startReporting, const ThreadList& threads);
// Stop it
extern void RecordStop(const ThreadList& threads);

// Get results since last interval -- may be called while running
extern BenchmarkIntervalResults ReportIntervalResults(
    const ThreadList& threads);
// Get total results -- must be called after stop
extern BenchmarkResults ReportResults();
// And clean it up. Don't call before reporting.
extern void EndReporting();

// Record an error connecting
extern void RecordSocketError();
// Report any time we open a connection
extern void RecordConnectionOpen();

// Call ReportResults and print to a file
extern void PrintShortResults(std::ostream& out, const std::string& runName,
                              size_t numThreads, int connections);
extern void PrintFullResults(std::ostream& out);
// Call ReportIntervalResults and print to a file
extern void ReportInterval(std::ostream& out, const ThreadList& threads,
                           int totalDuration, bool warmup);
// If ReportInterval is not being called, call this instead to ensure
// that the CPU samples are happening regularly so
// that we get a good average.
extern void SampleCPU();
// Print a CSV header for the "short" reporting format
extern void PrintReportingHeader(std::ostream& out);

}  // namespace apib

#endif  // APIB_REPORTING_H