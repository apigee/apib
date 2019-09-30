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
#include <assert.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <numeric>
#include <regex>
#include <valarray>
#include <vector>

#include "src/apib_cpu.h"
#include "src/apib_time.h"

using std::cerr;
using std::endl;

namespace apib {

static const std::string kCPUCmd("cpu\n");
static const std::string kMemCmd("mem\n");
static const std::regex kHostPort("^([^:]+):([0-9]+)$");

static std::mutex latch;
static volatile bool reporting = 0;
static bool cpuAvailable = false;
static int32_t completedRequests = 0;
static int32_t successfulRequests = 0;
static int32_t intervalSuccessful = 0;
static int32_t unsuccessfulRequests = 0;
static int32_t socketErrors = 0;
static int32_t connectionsOpened = 0;

static int64_t startTime;
static int64_t stopTime;
static int64_t intervalStartTime;

static std::vector<int64_t> latencies;

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

void RecordResult(int code, int64_t latency) {
  if (!reporting) {
    return;
  }

  std::lock_guard<std::mutex> lock(latch);
  completedRequests++;
  if ((code >= 200) && (code < 300)) {
    successfulRequests++;
    intervalSuccessful++;
  } else {
    unsuccessfulRequests++;
  }
  latencies.push_back(latency);
}

void RecordSocketError(void) {
  if (!reporting) {
    return;
  }
  std::lock_guard<std::mutex> lock(latch);
  socketErrors++;
}

void RecordConnectionOpen(void) {
  if (!reporting) {
    return;
  }
  std::lock_guard<std::mutex> lock(latch);
  connectionsOpened++;
}

void RecordByteCounts(int64_t sent, int64_t received) {
  std::lock_guard<std::mutex> lock(latch);
  totalBytesSent += sent;
  totalBytesReceived += received;
}

void RecordInit(const std::string& monitorHost, const std::string& host2) {
  int err = cpu_Init();
  cpuAvailable = (err == 0);
  remoteMonitorHost = monitorHost;
  remote2MonitorHost = host2;
}

void RecordStart(bool startReporting) {
  std::lock_guard<std::mutex> lock(latch);
  /* When we warm up we want to zero these out before continuing */
  completedRequests = 0;
  successfulRequests = 0;
  intervalSuccessful = 0;
  unsuccessfulRequests = 0;
  socketErrors = 0;
  connectionsOpened = 0;
  totalBytesSent = 0;
  totalBytesReceived = 0;
  latencies.clear();

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

  startTime = apib_GetTime();
  intervalStartTime = startTime;

  clientSamples.clear();
  remoteSamples.clear();
  remote2Samples.clear();
}

void RecordStop(void) {
  std::lock_guard<std::mutex> lock(latch);
  clientMem = cpu_GetMemoryUsage();

  if (remoteCpuSocket != 0) {
    remoteMem = getRemoteStat(kMemCmd, &remoteCpuSocket);
  }
  if (remote2CpuSocket != 0) {
    remote2Mem = getRemoteStat(kMemCmd, &remote2CpuSocket);
  }

  reporting = 0;
  stopTime = apib_GetTime();
}

BenchmarkIntervalResults ReportIntervalResults() {
  BenchmarkIntervalResults r;
  std::lock_guard<std::mutex> lock(latch);
  const int64_t now = apib_GetTime();
  r.successfulRequests = intervalSuccessful;
  r.intervalTime = apib_Seconds(now - intervalStartTime);
  r.elapsedTime = apib_Seconds(now - startTime);
  r.averageThroughput = (double)intervalSuccessful / r.intervalTime;

  intervalSuccessful = 0;
  intervalStartTime = now;
  return r;
}

void ReportInterval(std::ostream& out, int totalDuration, int warmup) {
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

  const BenchmarkIntervalResults r = ReportIntervalResults();
  const std::string warm = (warmup ? "Warming up: " : "");

  out << std::fixed;
  out << warm << '(' << std::setprecision(0) << r.elapsedTime << " / "
      << totalDuration << ") " << std::setprecision(3) << r.averageThroughput;
  if (cpu > 0.0) {
    out << ' ' << std::setprecision(0) << cpu * 100.0 << "% cpu";
  }
  if (remoteCpu > 0.0) {
    out << ' ' << std::setprecision(0) << remoteCpu * 100.0 << "% remote cpu";
  }
  out << endl;
}

static bool compareLLongs(const int64_t& l1, const int64_t& l2) {
  return l1 < l2;
}

static int64_t getLatencyPercent(int percent) {
  if (latencies.empty()) {
    return 0;
  }
  if (percent == 100) {
    return latencies[latencies.size() - 1];
  }
  size_t index = (latencies.size() / 100.0) * percent;
  return latencies[index];
}

static int64_t getAverageLatency(void) {
  if (latencies.empty()) {
    return 0LL;
  }
  return std::accumulate(latencies.begin(), latencies.end(), 0LL) /
         (int64_t)latencies.size();
}

static double getLatencyStdDev(void) {
  if (latencies.empty()) {
    return 0.0;
  }
  unsigned long avg = apib_Milliseconds(getAverageLatency());
  double differences = 0.0;

  std::for_each(latencies.begin(), latencies.end(),
                [&differences, avg](int64_t& l) {
                  differences += pow(apib_Milliseconds(l) - avg, 2.0);
                });

  return sqrt(differences / (double)latencies.size());
}

static double getAverageCpu(const std::vector<double>& s) {
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
  BenchmarkResults r;
  std::lock_guard<std::mutex> lock(latch);
  std::sort(latencies.begin(), latencies.end(), compareLLongs);
  r.completedRequests = completedRequests;
  r.successfulRequests = successfulRequests;
  r.unsuccessfulRequests = unsuccessfulRequests;
  r.socketErrors = socketErrors;
  r.connectionsOpened = connectionsOpened;
  r.totalBytesSent = totalBytesSent;
  r.totalBytesReceived = totalBytesReceived;

  const int64_t rawElapsed = stopTime - startTime;
  r.elapsedTime = apib_Seconds(rawElapsed);
  r.averageLatency = apib_Milliseconds(getAverageLatency());
  r.latencyStdDev = getLatencyStdDev();
  for (int i = 0; i < 101; i++) {
    r.latencies[i] = apib_Milliseconds(getLatencyPercent(i));
  }
  r.averageThroughput = (double)completedRequests / r.elapsedTime;
  r.averageSendBandwidth = (totalBytesSent * 8.0 / 1048576.0) / r.elapsedTime;
  r.averageReceiveBandwidth =
      (totalBytesReceived * 8.0 / 1048576.0) / r.elapsedTime;
  return r;
}

void PrintFullResults(std::ostream& out) {
  const BenchmarkResults r = ReportResults();

  out << std::fixed << std::setprecision(3);
  out << "Duration:             " << r.elapsedTime << " seconds" << endl;
  out << "Attempted requests:   " << r.completedRequests << endl;
  out << "Successful requests:  " << r.successfulRequests << endl;
  out << "Non-200 results:      " << r.unsuccessfulRequests << endl;
  out << "Connections opened:   " << r.connectionsOpened << endl;
  out << "Socket errors:        " << r.socketErrors << endl;
  out << endl;
  out << "Throughput:           " << r.averageThroughput << " requests/second"
      << endl;
  out << "Average latency:      " << r.averageLatency << " milliseconds"
      << endl;
  out << "Minimum latency:      " << r.latencies[0] << " milliseconds" << endl;
  out << "Maximum latency:      " << r.latencies[100] << " milliseconds"
      << endl;
  out << "Latency std. dev:     " << r.latencyStdDev << " milliseconds" << endl;
  out << "50% latency:          " << r.latencies[50] << " milliseconds" << endl;
  out << "90% latency:          " << r.latencies[90] << " milliseconds" << endl;
  out << "98% latency:          " << r.latencies[98] << " milliseconds" << endl;
  out << "99% latency:          " << r.latencies[99] << " milliseconds" << endl;
  out << endl;
  out << std::setprecision(0);
  if (!clientSamples.empty()) {
    out << "Client CPU average:    " << getAverageCpu(clientSamples) * 100.0
        << endl;
    out << "Client CPU max:        " << getMaxCpu(clientSamples) * 100.0
        << endl;
  }
  out << "Client memory usage:     " << clientMem * 100.0 << endl;
  if (!remoteSamples.empty()) {
    out << "Remote CPU average:    " << getAverageCpu(remoteSamples) * 100.0
        << endl;
    out << "Remote CPU max:        " << getMaxCpu(remoteSamples) * 100.0
        << endl;
    out << "Remote memory usage:   " << remoteMem * 100.0 << endl;
  }
  if (!remote2Samples.empty()) {
    out << "Remote 2 CPU average:    " << getAverageCpu(remote2Samples) * 100.0
        << '%' << endl;
    out << "Remote 2 CPU max:        " << getMaxCpu(remote2Samples) * 100.0
        << '%' << endl;
    out << "Remote 2 memory usage:   " << remote2Mem * 100.0 << '%' << endl;
  }
  out << endl;
  out << std::setprecision(2);
  out << "Total bytes sent:      " << r.totalBytesSent / 1048576.0
      << " megabytes" << endl;
  out << "Total bytes received:  " << r.totalBytesReceived / 1048576.0
      << " megabytes" << endl;
  out << "Send bandwidth:        " << r.averageSendBandwidth
      << " megabits / second" << endl;
  out << "Receive bandwidth:     " << r.averageReceiveBandwidth
      << " megabits / second" << endl;
}

void PrintShortResults(std::ostream& out, const std::string& runName,
                       int threads, int connections) {
  const BenchmarkResults r = ReportResults();

  /*
  name,throughput,avg.
  latency,threads,connections,duration,completed,successful,errors,sockets,min.
  latency,max. latency,50%,90%,98%,99%
   */
  out << std::fixed << std::setprecision(3) << runName << ','
      << r.averageThroughput << ',' << r.averageLatency << threads << ','
      << connections << ',' << r.elapsedTime << ',' << r.completedRequests
      << ',' << r.successfulRequests << ',' << r.unsuccessfulRequests << ','
      << r.connectionsOpened << ',' << r.latencies[0] << ',' << r.latencies[100]
      << ',' << r.latencies[50] << ',' << r.latencies[90] << ','
      << r.latencies[98] << ',' << r.latencies[99] << ',' << r.latencyStdDev
      << std::setprecision(0) << getAverageCpu(clientSamples) * 100.0 << ','
      << getAverageCpu(remoteSamples) * 100.0 << ','
      << getAverageCpu(remote2Samples) * 100.0 << ',' << std::setprecision(2)
      << clientMem * 100.0 << ',' << remoteMem * 100.0 << ','
      << remote2Mem * 100.0 << ',' << r.averageSendBandwidth << ','
      << r.averageReceiveBandwidth << endl;
}

void PrintReportingHeader(std::ostream& out) {
  out << "Name,Throughput,Avg. Latency,Threads,Connections,Duration,"
         "Completed,Successful,Errors,Sockets,"
         "Min. latency,Max. latency,50%% Latency,90%% Latency,"
         "98%% Latency,99%% Latency,Latency Std Dev,Avg Client CPU,"
         "Avg Server CPU,Avg Server 2 CPU,"
         "Client Mem Usage,Server Mem,Server 2 Mem,"
         "Avg. Send Bandwidth,Avg. Recv. Bandwidth"
      << endl;
}

void EndReporting(void) {
  if (remoteCpuSocket != 0) {
    close(remoteCpuSocket);
  }
  if (remote2CpuSocket != 0) {
    close(remote2CpuSocket);
  }
}

}  // namespace apib
