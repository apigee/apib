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

#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  long completedRequests;
  long successfulRequests;
  long unsuccessfulRequests;
  long socketErrors;
  long connectionsOpened;
  long long totalBytesSent;
  long long totalBytesReceived;

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
} BenchmarkResults;

typedef struct {
  long successfulRequests;
  // In seconds
  double elapsedTime;
  double intervalTime;
  // In tps, for this interval
  double averageThroughput;
} BenchmarkIntervalResults;

// One time initialization
extern void RecordInit(const char* monitorHost, const char* monitor2Host);

// Start a reporting run
extern void RecordStart(int startReporting);
// Stop it
extern void RecordStop(void);

// Get results since last interval -- may be called while running
extern void ReportIntervalResults(BenchmarkIntervalResults* r);
// Get total results -- must be called after stop
extern void ReportResults(BenchmarkResults* r);
// And clean it up. Don't call before reporting.
extern void EndReporting(void);

// Record an HTTP response and response code
extern void RecordResult(int code, long long latency);
// Record an error connecting
extern void RecordSocketError(void);
// Report any time we open a connection
extern void RecordConnectionOpen(void);
// Add to the total number of bytes processes
extern void RecordByteCounts(long long sent, long long received);

// Call ReportResults and print to a file
extern void PrintShortResults(FILE* out, const char* runName, int threads, int connections);
extern void PrintFullResults(FILE* out);
// Call ReportIntervalResults and print to a file
extern void ReportInterval(FILE* out, int totalDuration, int warmup);
// Print a CSV header for the "short" reporting format
extern void PrintReportingHeader(FILE* out);

#ifdef __cplusplus
}
#endif

#endif  // APIB_REPORTING_H