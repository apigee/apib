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

#include "src/apib_reporting.h"

#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <cassert>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <numeric>
#include <regex>
#include <valarray>
#include <vector>

#include "absl/strings/str_format.h"
#include "src/apib_cpu.h"
#include "src/apib_time.h"

using absl::StrFormat;
using std::cerr;
using std::endl;

namespace apib {

static const std::string kCPUCmd("cpu\n");
static const std::string kMemCmd("mem\n");
static const std::regex kHostPort("^([^:]+):([0-9]+)$");

static std::mutex latch;
static volatile bool reporting = 0;
static bool cpuAvailable = false;
static std::atomic_int_fast32_t socketErrors;
static std::atomic_int_fast32_t connectionsOpened;

static int_fast32_t successfulRequests;
static int_fast32_t unsuccessfulRequests;

static int64_t startTime;
static int64_t stopTime;
static int64_t intervalStartTime;

static std::vector<std::unique_ptr<Counters>> accumulatedResults;

static std::vector<double> clientSamples;
static std::vector<double> remoteSamples;
static std::vector<double> remote2Samples;

static double clientMem = 0.0;
static double remoteMem = 0.0;
static double remote2Mem = 0.0;

static CPUUsage cpuUsage;
static int remoteCpuSocket = 0;
static int remote2CpuSocket = 0;
static std::string remoteMonitorHost;
static std::string remote2MonitorHost;

static int64_t totalBytesSent = 0LL;
static int64_t totalBytesReceived = 0LL;

static void connectMonitor(const std::string& hn, int* fd) {
  assert(fd != NULL);
  std::smatch hostPortMatch;
  if (!std::regex_match(hn, hostPortMatch, kHostPort)) {
    cerr << "Invalid monitor host \"" << hn << '\"' << endl;
    return;
  }

  assert(hostPortMatch.size() == 2);
  const std::string hostName = hostPortMatch[0];
  const int port = stoi(hostPortMatch[1]);

  struct addrinfo hints;
  // For now, look up only IP V4 addresses
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_protocol = 0;
  hints.ai_flags = 0;

  struct addrinfo* hostInfo = NULL;
  int err = getaddrinfo(hostName.c_str(), nullptr, &hints, &hostInfo);
  if (err != 0) {
    cerr << "Cannot look up remote monitoring host: " << errno << endl;
    goto done;
  }

  *fd = socket(hostInfo->ai_family, SOCK_STREAM, 0);
  assert(*fd > 0);

  // IP4 and IP6 versions of this should have port in same place
  ((struct sockaddr_in*)hostInfo->ai_addr)->sin_port = htons(port);

  err = connect(*fd, hostInfo->ai_addr, hostInfo->ai_addrlen);
  if (err != 0) {
    cerr << "Connection error: " << errno << endl
         << "Cannot connect to remote monitoring host \"" << hostName
         << " on port " << port << endl;
    close(*fd);
    goto done;
  }

done:
  if (hostInfo != NULL) {
    freeaddrinfo(hostInfo);
  }
}

static double getRemoteStat(const std::string& cmd, int* fd) {
  assert(fd != NULL);
  char buf[64];
  int rc;

  const ssize_t wc = write(*fd, cmd.data(), cmd.size());
  if (wc != (ssize_t)cmd.size()) {
    cerr << "Error writing to monitoring server: " << errno << endl;
    goto failure;
  }

  rc = read(*fd, buf, 64);
  if (rc <= 0) {
    cerr << "Error reading from monitoring server: " << errno << endl;
    goto failure;
  }

  return strtod(buf, NULL);

failure:
  close(*fd);
  *fd = 0;
  return 0.0;
}

void RecordSocketError(void) {
  if (!reporting) {
    return;
  }
  socketErrors++;
}

void RecordConnectionOpen(void) {
  if (!reporting) {
    return;
  }
  connectionsOpened++;
}

void RecordByteCounts(int64_t sent, int64_t received) {
  totalBytesSent += sent;
  totalBytesReceived += received;
}

void RecordInit(const std::string& monitorHost, const std::string& host2) {
  int err = cpu_Init();
  cpuAvailable = (err == 0);
  remoteMonitorHost = monitorHost;
  remote2MonitorHost = host2;
}

void RecordStart(bool startReporting, const ThreadList& threads) {
  /* When we warm up we want to zero these out before continuing */
  std::lock_guard<std::mutex> lock(latch);
  successfulRequests = 0;
  unsuccessfulRequests = 0;
  socketErrors = 0;
  connectionsOpened = 0;
  totalBytesSent = 0;
  totalBytesReceived = 0;
  accumulatedResults.clear();

  // We also want to zero out each thread's counters
  // since they may have started already!
  for (auto it = threads.cbegin(); it != threads.cend(); it++) {
    Counters* c = (*it)->exchangeCounters();
    delete c;
  }

  reporting = startReporting;
  cpu_GetUsage(&cpuUsage);

  if (!remoteMonitorHost.empty()) {
    if (remoteCpuSocket == 0) {
      connectMonitor(remoteMonitorHost, &remoteCpuSocket);
    } else {
      // Just re-set the CPU time
      getRemoteStat(kCPUCmd, &remoteCpuSocket);
    }
  }
  if (!remote2MonitorHost.empty()) {
    if (remote2CpuSocket == 0) {
      connectMonitor(remote2MonitorHost, &remote2CpuSocket);
    } else {
      // Just re-set the CPU time
      getRemoteStat(kCPUCmd, &remote2CpuSocket);
    }
  }

  startTime = GetTime();
  intervalStartTime = startTime;

  clientSamples.clear();
  remoteSamples.clear();
  remote2Samples.clear();
}

void RecordStop(const ThreadList& threads) {
  clientMem = cpu_GetMemoryUsage();

  if (remoteCpuSocket != 0) {
    remoteMem = getRemoteStat(kMemCmd, &remoteCpuSocket);
  }
  if (remote2CpuSocket != 0) {
    remote2Mem = getRemoteStat(kMemCmd, &remote2CpuSocket);
  }

  reporting = false;
  for (auto it = threads.cbegin(); it != threads.cend(); it++) {
    Counters* c = (*it)->exchangeCounters();
    totalBytesReceived += c->bytesRead;
    totalBytesSent += c->bytesWritten;
    successfulRequests += c->successfulRequests;
    unsuccessfulRequests += c->failedRequests;
    accumulatedResults.push_back(std::unique_ptr<Counters>(c));
  }
  stopTime = GetTime();
}

BenchmarkIntervalResults ReportIntervalResults(const ThreadList& threads) {
  int_fast32_t intervalSuccesses = 0LL;
  int_fast32_t intervalFailures = 0LL;
  const int64_t now = GetTime();

  for (auto it = threads.cbegin(); it != threads.cend(); it++) {
    Counters* c = (*it)->exchangeCounters();
    totalBytesReceived += c->bytesRead;
    totalBytesSent += c->bytesWritten;
    intervalSuccesses += c->successfulRequests;
    intervalFailures += c->failedRequests;
    accumulatedResults.push_back(std::unique_ptr<Counters>(c));
  }

  // "exchangeCounters" clears thread-specific counters. Transfer new totals
  // to the grand total for an accurate result.
  successfulRequests += intervalSuccesses;
  unsuccessfulRequests += intervalFailures;

  BenchmarkIntervalResults r;
  r.successfulRequests = intervalSuccesses;
  r.intervalTime = Seconds(now - intervalStartTime);
  r.elapsedTime = Seconds(now - startTime);
  r.averageThroughput = (double)r.successfulRequests / r.intervalTime;
  intervalStartTime = now;
  return r;
}

void ReportInterval(std::ostream& out, const ThreadList& threads,
                    int totalDuration, bool warmup) {
  double cpu = 0.0;
  double remoteCpu = 0.0;
  double remote2Cpu = 0.0;

  if (remoteCpuSocket != 0) {
    remoteCpu = getRemoteStat(kCPUCmd, &remoteCpuSocket);
    remoteSamples.push_back(remoteCpu);
  }
  if (remote2CpuSocket != 0) {
    remote2Cpu = getRemoteStat(kCPUCmd, &remote2CpuSocket);
    remote2Samples.push_back(remote2Cpu);
  }
  cpu = cpu_GetInterval(&cpuUsage);
  clientSamples.push_back(cpu);

  const BenchmarkIntervalResults r = ReportIntervalResults(threads);
  const std::string warm = (warmup ? "Warming up: " : "");

  out << StrFormat("%s(%.0f / %i) %.3f", warm, r.elapsedTime, totalDuration,
                   r.averageThroughput);
  if (cpu > 0.0) {
    out << StrFormat(" %.0f%% cpu", cpu * 100.0);
  }
  if (remoteCpu > 0.0) {
    out << StrFormat(" %.0f%% remote cpu", remoteCpu * 100.0);
  }
  out << endl;
}

static int64_t getLatencyPercent(const std::vector<int_fast64_t>& latencies,
                                 int percent) {
  if (latencies.empty()) {
    return 0;
  }
  if (percent == 100) {
    return latencies[latencies.size() - 1];
  }
  size_t index = (latencies.size() / 100.0) * percent;
  return latencies[index];
}

static int64_t getAverageLatency(const std::vector<int_fast64_t>& latencies) {
  if (latencies.empty()) {
    return 0LL;
  }
  return std::accumulate(latencies.begin(), latencies.end(), 0LL) /
         (int64_t)latencies.size();
}

static double getLatencyStdDev(const std::vector<int_fast64_t>& latencies) {
  if (latencies.empty()) {
    return 0.0;
  }
  unsigned long avg = Milliseconds(getAverageLatency(latencies));
  double differences = 0.0;

  std::for_each(latencies.begin(), latencies.end(),
                [&differences, avg](const int_fast64_t& l) {
                  differences += pow(Milliseconds(l) - avg, 2.0);
                });

  return sqrt(differences / (double)latencies.size());
}

static double getAverageCpu(const std::vector<double>& s) {
  if (s.empty()) {
    return 0.0;
  }
  double total = 0.0;
  std::for_each(s.cbegin(), s.cend(), [&total](double d) { total += d; });
  return total / s.size();
}

static double getMaxCpu(const std::vector<double>& s) {
  if (s.empty()) {
    return 0.0;
  }
  return *(std::max_element(s.cbegin(), s.cend()));
}

BenchmarkResults ReportResults() {
  std::vector<int_fast64_t> allLatencies;
  for (auto it = accumulatedResults.begin(); it != accumulatedResults.end();
       it++) {
    for (auto lit = (*it)->latencies.begin(); lit != (*it)->latencies.end();
         lit++) {
      allLatencies.push_back(*lit);
    }
  }
  std::sort(allLatencies.begin(), allLatencies.end());

  BenchmarkResults r;
  std::lock_guard<std::mutex> lock(latch);

  r.completedRequests = successfulRequests + unsuccessfulRequests;
  r.successfulRequests = successfulRequests;
  r.unsuccessfulRequests = unsuccessfulRequests;
  r.socketErrors = socketErrors;
  r.connectionsOpened = connectionsOpened;
  r.totalBytesSent = totalBytesSent;
  r.totalBytesReceived = totalBytesReceived;

  const int64_t rawElapsed = stopTime - startTime;
  r.elapsedTime = Seconds(rawElapsed);
  r.averageLatency = Milliseconds(getAverageLatency(allLatencies));
  r.latencyStdDev = getLatencyStdDev(allLatencies);
  for (int i = 0; i < 101; i++) {
    r.latencies[i] = Milliseconds(getLatencyPercent(allLatencies, i));
  }
  r.averageThroughput = (double)r.completedRequests / r.elapsedTime;
  r.averageSendBandwidth = (totalBytesSent * 8.0 / 1048576.0) / r.elapsedTime;
  r.averageReceiveBandwidth =
      (totalBytesReceived * 8.0 / 1048576.0) / r.elapsedTime;
  return r;
}

void PrintFullResults(std::ostream& out) {
  const BenchmarkResults r = ReportResults();

  out << StrFormat("Duration:             %.3f seconds", r.elapsedTime) << endl;
  out << StrFormat("Attempted requests:   %i", r.completedRequests) << endl;
  out << StrFormat("Successful requests:  %i", r.successfulRequests) << endl;
  out << StrFormat("Non-200 results:      %i", r.unsuccessfulRequests) << endl;
  out << StrFormat("Connections opened:   %i", r.connectionsOpened) << endl;
  out << StrFormat("Socket errors:        %i", r.socketErrors) << endl;
  out << endl;
  out << StrFormat("Throughput:           %.3f requests/second",
                   r.averageThroughput)
      << endl;
  out << StrFormat("Average latency:      %.3f milliseconds", r.averageLatency)
      << endl;
  out << StrFormat("Minimum latency:      %.3f milliseconds", r.latencies[0])
      << endl;
  out << StrFormat("Maximum latency:      %.3f milliseconds", r.latencies[100])
      << endl;
  out << StrFormat("Latency std. dev:     %.3f milliseconds", r.latencyStdDev)
      << endl;
  out << StrFormat("50%% latency:          %.3f milliseconds", r.latencies[50])
      << endl;
  out << StrFormat("90%% latency:          %.3f milliseconds", r.latencies[90])
      << endl;
  out << StrFormat("98%% latency:          %.3f milliseconds", r.latencies[98])
      << endl;
  out << StrFormat("99%% latency:          %.3f milliseconds", r.latencies[99])
      << endl;
  out << endl;
  if (!clientSamples.empty()) {
    out << StrFormat("Client CPU average:    %.0f%%",
                     getAverageCpu(clientSamples) * 100.0)
        << endl;
    out << StrFormat("Client CPU max:        %.0f%%",
                     getMaxCpu(clientSamples) * 100.0)
        << endl;
  }
  out << StrFormat("Client memory usage:   %.0f%%", clientMem * 100.0) << endl;
  if (!remoteSamples.empty()) {
    out << StrFormat("Remote CPU average:    %.0f%%",
                     getAverageCpu(remoteSamples) * 100.0)
        << endl;
    out << StrFormat("Remote CPU max:        %.0f%%",
                     getMaxCpu(remoteSamples) * 100.0)
        << endl;
    out << StrFormat("Remote memory usage:   %.0f%%", remoteMem * 100.0)
        << endl;
  }
  if (!remote2Samples.empty()) {
    out << StrFormat("Remote 2 CPU average:    %.0f%%",
                     getAverageCpu(remote2Samples) * 100.0)
        << endl;
    out << StrFormat("Remote 2 CPU max:        %.0f%%",
                     getMaxCpu(remote2Samples) * 100.0)
        << endl;
    out << StrFormat("Remote 2 memory usage:   %.0f%%", remote2Mem * 100.0)
        << endl;
  }
  out << endl;
  out << StrFormat("Total bytes sent:      %.2f megabytes",
                   r.totalBytesSent / 1048576.0)
      << endl;
  out << StrFormat("Total bytes received:  %.2f megabytes",
                   r.totalBytesReceived / 1048576.0)
      << endl;
  out << StrFormat("Send bandwidth:        %.2f megabits / second",
                   r.averageSendBandwidth)
      << endl;
  out << StrFormat("Receive bandwidth:     %.2f megabits / second",
                   r.averageReceiveBandwidth)
      << endl;
}

void PrintShortResults(std::ostream& out, const std::string& runName,
                       size_t numThreads, int connections) {
  const BenchmarkResults r = ReportResults();

  // See "PrintReportingHeader for column names
  out << StrFormat(
             "%s,%.3f,%.3f,%i,%i,%.3f,%i,%i,%i,%i,%.3f,%.3f,%.3f,%.3f,%.3f,%."
             "3f,%.3f,%.0f,%.0f,%.0f,%.0f,%.0f,%.0f,%.2f,%.2f",
             runName, r.averageThroughput, r.averageLatency, numThreads,
             connections, r.elapsedTime, r.completedRequests,
             r.successfulRequests, r.socketErrors, r.connectionsOpened,
             r.latencies[0], r.latencies[100], r.latencies[50], r.latencies[90],
             r.latencies[98], r.latencies[99], r.latencyStdDev,
             getAverageCpu(clientSamples) * 100.0,
             getAverageCpu(remoteSamples) * 100.0,
             getAverageCpu(remote2Samples) * 100.0, clientMem * 100.0,
             remoteMem * 100.0, remote2Mem * 100.0, r.averageSendBandwidth,
             r.averageReceiveBandwidth)
      << endl;
}

void PrintReportingHeader(std::ostream& out) {
  out << "Name,Throughput,Avg. Latency,Threads,Connections,Duration,"
         "Completed,Successful,Errors,Sockets,"
         "Min. latency,Max. latency,50% Latency,90% Latency,"
         "98% Latency,99% Latency,Latency Std Dev,Avg Client CPU,"
         "Avg Server CPU,Avg Server 2 CPU,"
         "Client Mem Usage,Server Mem,Server 2 Mem,"
         "Avg. Send Bandwidth,Avg. Recv. Bandwidth"
      << endl;
}

void EndReporting() {
  if (remoteCpuSocket != 0) {
    close(remoteCpuSocket);
  }
  if (remote2CpuSocket != 0) {
    close(remote2CpuSocket);
  }
}

}  // namespace apib
