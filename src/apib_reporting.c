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
#include <math.h>
#include <netdb.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include "src/apib_cpu.h"
#include "src/apib_time.h"

#define NUM_CPU_SAMPLES 4
#define NUM_INITIAL_LATENCIES 8192

#define CPU_CMD "cpu\n"
#define MEM_CMD "mem\n"

typedef struct {
  size_t count;
  size_t size;
  double* samples;
} CPUSamples;

static pthread_mutex_t latch;
static volatile int reporting = 0;
static int cpuAvailable = 0;
static long completedRequests = 0;
static long successfulRequests = 0;
static long intervalSuccessful = 0;
static long unsuccessfulRequests = 0;
static long socketErrors = 0;
static long connectionsOpened = 0;

static long long startTime;
static long long stopTime;
static long long intervalStartTime;

static long long* latencies = NULL;
static size_t latenciesCount = 0;
static size_t latenciesSize = 0;

static CPUSamples clientSamples;
static CPUSamples remoteSamples;
static CPUSamples remote2Samples;

static double clientMem = 0.0;
static double remoteMem = 0.0;
static double remote2Mem = 0.0;

static CPUUsage cpuUsage;
static int remoteCpuSocket = 0;
static int remote2CpuSocket = 0;
static const char* remoteMonitorHost = NULL;
static const char* remote2MonitorHost = NULL;

static unsigned long long totalBytesSent = 0LL;
static unsigned long long totalBytesReceived = 0LL;

static void initSamples(CPUSamples* s) {
  s->count = 0;
  s->size = NUM_CPU_SAMPLES;
  s->samples = (double*)malloc(sizeof(double) * NUM_CPU_SAMPLES);
}

static void freeSamples(CPUSamples* s) { free(s->samples); }

static void addSample(double sample, CPUSamples* s) {
  if (s->count >= s->size) {
    s->size *= 2;
    s->samples = (double*)realloc(s->samples, sizeof(double) * s->size);
  }
  s->samples[s->count] = sample;
  s->count++;
}

static void connectMonitor(const char* hn, int* fd) {
  assert(fd != NULL);
  char* hostCopy = strdup(hn);
  char* sp;

  const char* hostName = strtok_r(hostCopy, ":", &sp);
  assert(hostName != NULL);
  const char* portName = strtok_r(NULL, "", &sp);
  if (portName == NULL) {
    printf("Invalid monitor host: \"%s\"\n", hn);
    goto done;
  }

  char* pe;
  short port = (short)strtol(portName, &pe, 0);
  if (pe == portName) {
    printf("Invalid monitor port number: \"\%s\"\n", portName);
    goto done;
  }

  struct addrinfo hints;

  // For now, look up only IP V4 addresses
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_protocol = 0;
  hints.ai_flags = 0;

  struct addrinfo* hostInfo = NULL;
  int err = getaddrinfo(hostName, NULL, &hints, &hostInfo);
  if (err != 0) {
    perror("Cannot look up remote monitoring host");
    goto done;
  }

  *fd = socket(hostInfo->ai_family, SOCK_STREAM, 0);
  assert(*fd > 0);

  // IP4 and IP6 versions of this should have port in same place
  ((struct sockaddr_in*)hostInfo->ai_addr)->sin_port = htons(port);

  err = connect(*fd, hostInfo->ai_addr, hostInfo->ai_addrlen);
  if (err != 0) {
    perror("Connection error");
    printf("Cannot connect to remote monitoring host \"%s\" port %i\n",
           hostName, port);
    close(*fd);
    goto done;
  }

done:
  free(hostCopy);
  if (hostInfo != NULL) {
    freeaddrinfo(hostInfo);
  }
}

static double getRemoteStat(const char* cmd, int* fd) {
  assert(fd != NULL);
  char buf[64];

  const size_t cmdLen = strlen(cmd);
  const int wc = write(*fd, cmd, cmdLen);
  if (wc != cmdLen) {
    perror("Error writing to monitoring server");
    goto failure;
  }

  const int rc = read(*fd, buf, 64);
  if (rc <= 0) {
    perror("Error reading from monitoring server");
    goto failure;
  }

  return strtod(buf, NULL);

failure:
  close(*fd);
  *fd = 0;
  return 0.0;
}

void RecordResult(int code, long long latency) {
  if (!reporting) {
    return;
  }
  pthread_mutex_lock(&latch);
  completedRequests++;
  if ((code >= 200) && (code < 300)) {
    successfulRequests++;
    intervalSuccessful++;
  } else {
    unsuccessfulRequests++;
  }
  if (latenciesCount == latenciesSize) {
    latenciesSize *= 2;
    latencies = (long long*)malloc(sizeof(long long) * latenciesSize);
  }
  latencies[latenciesCount] = latency;
  latenciesCount++;
  pthread_mutex_unlock(&latch);
}

void RecordSocketError(void) {
  if (!reporting) {
    return;
  }
  pthread_mutex_lock(&latch);
  socketErrors++;
  pthread_mutex_unlock(&latch);
}

void RecordConnectionOpen(void) {
  if (!reporting) {
    return;
  }
  pthread_mutex_lock(&latch);
  connectionsOpened++;
  pthread_mutex_unlock(&latch);
}

void RecordByteCounts(long long sent, long long received) {
  pthread_mutex_lock(&latch);
  totalBytesSent += sent;
  totalBytesReceived += received;
  pthread_mutex_unlock(&latch);
}

void RecordInit(const char* monitorHost, const char* host2) {
  pthread_mutex_init(&latch, NULL);
  int err = cpu_Init();
  cpuAvailable = (err == 0);
  latenciesSize = NUM_INITIAL_LATENCIES;
  latencies = (long long*)malloc(sizeof(long long) * NUM_INITIAL_LATENCIES);

  if (monitorHost != NULL) {
    remoteMonitorHost = monitorHost;
  }
  if (host2 != NULL) {
    remote2MonitorHost = host2;
  }
}

void RecordStart(int startReporting) {
  pthread_mutex_lock(&latch);
  /* When we warm up we want to zero these out before continuing */
  completedRequests = 0;
  successfulRequests = 0;
  intervalSuccessful = 0;
  unsuccessfulRequests = 0;
  socketErrors = 0;
  connectionsOpened = 0;
  totalBytesSent = 0;
  totalBytesReceived = 0;
  latenciesCount = 0;

  reporting = startReporting;
  cpu_GetUsage(&cpuUsage);

  if (remoteMonitorHost != NULL) {
    if (remoteCpuSocket == 0) {
      connectMonitor(remoteMonitorHost, &remoteCpuSocket);
    } else {
      // Just re-set the CPU time
      getRemoteStat(CPU_CMD, &remoteCpuSocket);
    }
  }
  if (remote2MonitorHost != NULL) {
    if (remote2CpuSocket == 0) {
      connectMonitor(remote2MonitorHost, &remote2CpuSocket);
    } else {
      // Just re-set the CPU time
      getRemoteStat(CPU_CMD, &remote2CpuSocket);
    }
  }

  startTime = apib_GetTime();
  intervalStartTime = startTime;

  initSamples(&clientSamples);
  initSamples(&remoteSamples);
  initSamples(&remote2Samples);
  pthread_mutex_unlock(&latch);
}

void RecordStop(void) {
  pthread_mutex_lock(&latch);
  clientMem = cpu_GetMemoryUsage();

  if (remoteCpuSocket != 0) {
    remoteMem = getRemoteStat(MEM_CMD, &remoteCpuSocket);
  }
  if (remote2CpuSocket != 0) {
    remote2Mem = getRemoteStat(MEM_CMD, &remote2CpuSocket);
  }

  reporting = 0;
  stopTime = apib_GetTime();
  pthread_mutex_unlock(&latch);
}

void ReportIntervalResults(BenchmarkIntervalResults* r) {
  pthread_mutex_lock(&latch);
  const long long now = apib_GetTime();
  r->successfulRequests = intervalSuccessful;
  r->intervalTime = apib_Seconds(now - intervalStartTime);
  r->elapsedTime = apib_Seconds(now - startTime);
  r->averageThroughput = (double)intervalSuccessful / r->intervalTime;

  intervalSuccessful = 0;
  intervalStartTime = now;
  pthread_mutex_unlock(&latch);
}

void ReportInterval(FILE* out, int totalDuration, int warmup) {
  double cpu = 0.0;
  double remoteCpu = 0.0;
  double remote2Cpu = 0.0;

  if (!warmup) {
    if (remoteCpuSocket != 0) {
      remoteCpu = getRemoteStat(CPU_CMD, &remoteCpuSocket);
      addSample(remoteCpu, &remoteSamples);
    }
    if (remote2CpuSocket != 0) {
      remote2Cpu = getRemoteStat(CPU_CMD, &remote2CpuSocket);
      addSample(remote2Cpu, &remote2Samples);
    }
    cpu = cpu_GetInterval(&cpuUsage);
    addSample(cpu, &clientSamples);
  }

  BenchmarkIntervalResults r;
  ReportIntervalResults(&r);

  char* warm = (warmup ? "Warming up: " : "");

  if (remoteCpu != 0.0) {
    fprintf(out, "%s(%.3lf / %i) %.3lf %.0lf%% cpu %.0lf%% remote cpu\n", warm,
            r.elapsedTime, totalDuration, r.averageThroughput, cpu * 100.0,
            remoteCpu * 100.0);
  } else {
    fprintf(out, "%s(%.3lf / %i) %.3lf %.0lf%% cpu\n", warm, r.elapsedTime,
            totalDuration, r.averageThroughput, cpu * 100.0);
  }
}

static int compareLLongs(const void* a1, const void* a2) {
  const long long* l1 = (const long long*)a1;
  const long long* l2 = (const long long*)a2;

  if (*l1 < *l2) {
    return -1;
  } else if (*l1 > *l2) {
    return 1;
  }
  return 0;
}

static int compareDoubles(const void* a1, const void* a2) {
  const double* d1 = (const double*)a1;
  const double* d2 = (const double*)a2;

  if (*d1 < *d2) {
    return -1;
  } else if (*d1 > *d2) {
    return 1;
  }
  return 0;
}

static long long getLatencyPercent(int percent) {
  if (latenciesCount == 0) {
    return 0;
  }
  if (percent == 100) {
    return latencies[latenciesCount - 1];
  }
  size_t index = (latenciesCount / 100.0) * percent;
  return latencies[index];
}

static long long getAverageLatency(void) {
  if (latenciesCount == 0) {
    return 0;
  }
  long long totalLatency = 0LL;

  for (size_t i = 0; i < latenciesCount; i++) {
    totalLatency += latencies[i];
  }

  return totalLatency / (long long)latenciesCount;
}

static double getLatencyStdDev(void) {
  if (latenciesCount == 0) {
    return 0.0;
  }
  unsigned long avg = apib_Milliseconds(getAverageLatency());
  double differences = 0.0;

  for (unsigned int i = 0; i < latenciesCount; i++) {
    differences += pow(apib_Milliseconds(latencies[i]) - avg, 2.0);
  }

  return sqrt(differences / (double)latenciesCount);
}

static double getAverageCpu(const CPUSamples* s) {
  if (s->count == 0) {
    return 0.0;
  }
  double total = 0.0;

  for (size_t i = 0; i < s->count; i++) {
    total += s->samples[i];
  }

  return total / s->count;
}

static double getMaxCpu(CPUSamples* s) {
  if (s->count == 0) {
    return 0.0;
  }

  qsort(s->samples, s->count, sizeof(double), compareDoubles);
  return s->samples[s->count - 1];
}

void ReportResults(BenchmarkResults* r) {
  pthread_mutex_lock(&latch);

  qsort(latencies, latenciesCount, sizeof(long long), compareLLongs);
  r->completedRequests = completedRequests;
  r->successfulRequests = successfulRequests;
  r->unsuccessfulRequests = unsuccessfulRequests;
  r->socketErrors = socketErrors;
  r->connectionsOpened = connectionsOpened;
  r->totalBytesSent = totalBytesSent;
  r->totalBytesReceived = totalBytesReceived;

  const long long rawElapsed = stopTime - startTime;
  r->elapsedTime = apib_Seconds(rawElapsed);
  r->averageLatency = apib_Milliseconds(getAverageLatency());
  r->latencyStdDev = getLatencyStdDev();
  for (int i = 0; i < 101; i++) {
    r->latencies[i] = apib_Milliseconds(getLatencyPercent(i));
  }
  r->averageThroughput = (double)completedRequests / r->elapsedTime;
  r->averageSendBandwidth = (totalBytesSent * 8.0 / 1048576.0) / r->elapsedTime;
  r->averageReceiveBandwidth =
      (totalBytesReceived * 8.0 / 1048576.0) / r->elapsedTime;

  pthread_mutex_unlock(&latch);
}

void PrintFullResults(FILE* out) {
  BenchmarkResults r;
  ReportResults(&r);

  fprintf(out, "Duration:             %.3lf seconds\n", r.elapsedTime);
  fprintf(out, "Attempted requests:   %li\n", r.completedRequests);
  fprintf(out, "Successful requests:  %li\n", r.successfulRequests);
  fprintf(out, "Non-200 results:      %li\n", r.unsuccessfulRequests);
  fprintf(out, "Connections opened:   %li\n", r.connectionsOpened);
  fprintf(out, "Socket errors:        %li\n", r.socketErrors);
  fprintf(out, "\n");
  fprintf(out, "Throughput:           %.3lf requests/second\n",
          r.averageThroughput);
  fprintf(out, "Average latency:      %.3lf milliseconds\n", r.averageLatency);
  fprintf(out, "Minimum latency:      %.3lf milliseconds\n", r.latencies[0]);
  fprintf(out, "Maximum latency:      %.3lf milliseconds\n", r.latencies[100]);
  fprintf(out, "Latency std. dev:     %.3lf milliseconds\n", r.latencyStdDev);
  fprintf(out, "50%% latency:          %.3lf milliseconds\n", r.latencies[50]);
  fprintf(out, "90%% latency:          %.3lf milliseconds\n", r.latencies[90]);
  fprintf(out, "98%% latency:          %.3lf milliseconds\n", r.latencies[98]);
  fprintf(out, "99%% latency:          %.3lf milliseconds\n", r.latencies[99]);
  fprintf(out, "\n");
  if (clientSamples.count > 0) {
    fprintf(out, "Client CPU average:    %.0lf%%\n",
            getAverageCpu(&clientSamples) * 100.0);
    fprintf(out, "Client CPU max:        %.0lf%%\n",
            getMaxCpu(&clientSamples) * 100.0);
  }
  fprintf(out, "Client memory usage:    %.0lf%%\n", clientMem * 100.0);
  if (remoteSamples.count > 0) {
    fprintf(out, "Remote CPU average:    %.0lf%%\n",
            getAverageCpu(&remoteSamples) * 100.0);
    fprintf(out, "Remote CPU max:        %.0lf%%\n",
            getMaxCpu(&remoteSamples) * 100.0);
    fprintf(out, "Remote memory usage:   %.0lf%%\n", remoteMem * 100.0);
  }
  if (remote2Samples.count > 0) {
    fprintf(out, "Remote 2 CPU average:    %.0lf%%\n",
            getAverageCpu(&remote2Samples) * 100.0);
    fprintf(out, "Remote 2 CPU max:        %.0lf%%\n",
            getMaxCpu(&remote2Samples) * 100.0);
    fprintf(out, "Remote 2 memory usage:   %.0lf%%\n", remote2Mem * 100.0);
  }
  fprintf(out, "\n");

  fprintf(out, "Total bytes sent:      %.2lf megabytes\n",
          r.totalBytesSent / 1048576.0);
  fprintf(out, "Total bytes received:  %.2lf megabytes\n",
          r.totalBytesReceived / 1048576.0);
  fprintf(out, "Send bandwidth:        %.2lf megabits / second\n",
          r.averageSendBandwidth);
  fprintf(out, "Receive bandwidth:     %.2lf megabits / second\n",
          r.averageReceiveBandwidth);
}

void PrintShortResults(FILE* out, const char* runName, int threads,
                       int connections) {
  BenchmarkResults r;
  ReportResults(&r);

  /*
  name,throughput,avg.
  latency,threads,connections,duration,completed,successful,errors,sockets,min.
  latency,max. latency,50%,90%,98%,99%
   */
  fprintf(out,
          "%s,%.3lf,%.3lf,%i,%i,%.3lf,%li,%li,%li,%li,%.3lf,%.3lf,%.3lf,%.3lf,%"
          ".3lf,%.3lf,%.3lf,%.0lf,%.0lf,%.0lf,%.0lf,%.0lf,%.0lf,%.2lf,%.2lf\n",
          runName, r.averageThroughput, r.averageLatency, threads, connections,
          r.elapsedTime, r.completedRequests, r.successfulRequests,
          r.unsuccessfulRequests, r.connectionsOpened, r.latencies[0],
          r.latencies[100], r.latencies[50], r.latencies[90], r.latencies[98],
          r.latencies[99], r.latencyStdDev,
          getAverageCpu(&clientSamples) * 100.0,
          getAverageCpu(&remoteSamples) * 100.0,
          getAverageCpu(&remote2Samples) * 100.0, clientMem * 100.0,
          remoteMem * 100.0, remote2Mem * 100.0, r.averageSendBandwidth,
          r.averageReceiveBandwidth);
}

void PrintReportingHeader(FILE* out) {
  fprintf(out,
          "Name,Throughput,Avg. Latency,Threads,Connections,Duration,"
          "Completed,Successful,Errors,Sockets,"
          "Min. latency,Max. latency,50%% Latency,90%% Latency,"
          "98%% Latency,99%% Latency,Latency Std Dev,Avg Client CPU,"
          "Avg Server CPU,Avg Server 2 CPU,"
          "Client Mem Usage,Server Mem,Server 2 Mem,"
          "Avg. Send Bandwidth,Avg. Recv. Bandwidth\n");
}

void EndReporting(void) {
  if (remoteCpuSocket != 0) {
    close(remoteCpuSocket);
  }
  if (remote2CpuSocket != 0) {
    close(remote2CpuSocket);
  }
  if (latencies != NULL) {
    free(latencies);
  }
  freeSamples(&clientSamples);
  freeSamples(&remoteSamples);
  freeSamples(&remote2Samples);
}
