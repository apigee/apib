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
#include <ostream>
#include <string>
#include <vector>

namespace apib {

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
extern void RecordStart(bool startReporting);
// Stop it
extern void RecordStop();

// Get results since last interval -- may be called while running
extern BenchmarkIntervalResults ReportIntervalResults();
// Get total results -- must be called after stop
extern BenchmarkResults ReportResults();
// And clean it up. Don't call before reporting.
extern void EndReporting();

// Record an HTTP response and response code
extern void RecordResult(int code);
// Record an error connecting
extern void RecordSocketError();
// Report any time we open a connection
extern void RecordConnectionOpen();
// Add to the total number of bytes processed
extern void RecordByteCounts(int64_t sent, int64_t received);
// Add to the collection of latency counts
extern void RecordLatencies(const std::vector<int64_t>& l);

// Call ReportResults and print to a file
extern void PrintShortResults(std::ostream& out, const std::string& runName,
                              int threads, int connections);
extern void PrintFullResults(std::ostream& out);
// Call ReportIntervalResults and print to a file
extern void ReportInterval(std::ostream& out, int totalDuration, int warmup);
// Print a CSV header for the "short" reporting format
extern void PrintReportingHeader(std::ostream& out);

}  // namespace apib

#endif  // APIB_REPORTING_H