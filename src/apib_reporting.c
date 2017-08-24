/*
   Copyright 2013 Apigee Corp.

   Licensed under the Apache License, Version 2.0 (the "License");
   you may not use this file except in compliance with the License.
   You may obtain a copy of the License at

       http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software
   distributed under the License is distributed on an "AS IS" BASIS,
   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
   See the License for the specific language governing permissions and
   limitations under the License.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include <apr_atomic.h>
#include <apr_network_io.h>
#include <apr_time.h>

#include <apib.h>

#define NUM_CPU_SAMPLES 4

#define CPU_CMD "cpu\n"
#define MEM_CMD "mem\n"

typedef struct
{
  size_t count;
  size_t size;
  double* samples;
} CPUSamples;

static volatile int reporting = 0;
static volatile apr_uint32_t completedRequests = 0;
static volatile apr_uint32_t successfulRequests = 0;
static volatile apr_uint32_t intervalSuccessful = 0;
static volatile apr_uint32_t unsuccessfulRequests = 0;
static volatile apr_uint32_t socketErrors = 0;
static volatile apr_uint32_t connectionsOpened = 0;

static apr_time_t startTime;
static apr_time_t stopTime;
static apr_time_t intervalStartTime;

static unsigned long* latencies = NULL;
static unsigned int   latenciesCount = 0;

static CPUSamples clientSamples;
static CPUSamples remoteSamples;
static CPUSamples remote2Samples;

static double clientMem = 0.0;
static double remoteMem = 0.0;
static double remote2Mem = 0.0;

static CPUUsage cpuUsage;
static apr_socket_t* remoteCpuSocket = NULL;
static apr_socket_t* remote2CpuSocket = NULL;
static const char* remoteMonitorHost = NULL;
static const char* remote2MonitorHost = NULL;

static unsigned long long totalBytesSent = 0LL;
static unsigned long long totalBytesReceived = 0LL;

static void initSamples(CPUSamples* s) 
{
  s->count = 0;
  s->size = NUM_CPU_SAMPLES;
  s->samples = (double*)malloc(sizeof(double) * NUM_CPU_SAMPLES);
}

static void freeSamples(CPUSamples* s) 
{
  free(s->samples);
}

static void addSample(double sample, CPUSamples* s)
{
  if (s->count >= s->size) {
    s->size *= 2;
    s->samples = (double*)realloc(s->samples, sizeof(double) * s->size);
  }
  s->samples[s->count] = sample;
  s->count++;
}

static double microToMilli(unsigned long m)
{
  return (double)(m / 1000l) + ((m % 1000l) / 1000.0);
}

static double microToSecond(unsigned long m)
{
  return (double)(m / 1000000l) + ((m % 1000000l) / 1000000.0);
}

static void connectMonitor(const char* hostName, apr_socket_t** sock)
{
  apr_status_t s;
  apr_sockaddr_t* addr;
  char* host;
  char* scope;
  apr_port_t port;

  s = apr_parse_addr_port(&host, &scope, &port, hostName, MainPool);
  if (s != APR_SUCCESS) {
    return;
  }

  s = apr_sockaddr_info_get(&addr, host, APR_INET, port, 0, MainPool);
  if (s != APR_SUCCESS) {
    return;
  }

  s = apr_socket_create(sock, APR_INET, 
			SOCK_STREAM, APR_PROTO_TCP, MainPool);
  if (s != APR_SUCCESS) {
    return;
  }

  s = apr_socket_connect(*sock, addr);
  if (s != APR_SUCCESS) {
    *sock = NULL;
  }
}

static double getRemoteStat(const char* cmd, apr_socket_t** sock)
{
  char buf[64];
  apr_status_t s;
  apr_size_t len;

  len = strlen(cmd);
  s = apr_socket_send(*sock, cmd, &len);
  if (s != APR_SUCCESS) {
    goto failure;
  }

  len = 64;
  s = apr_socket_recv(*sock, buf, &len);
  if (s != APR_SUCCESS) {
    goto failure;
  }

  return strtod(buf, NULL);

 failure:
  apr_socket_close(*sock);
  *sock = NULL;
  return 0.0;
}

void RecordResult(IOArgs* args, int code, unsigned long latency)
{
  apr_atomic_inc32(&completedRequests);
  if ((code >= 200) && (code < 300)) {
    apr_atomic_inc32(&successfulRequests);
    apr_atomic_inc32(&intervalSuccessful);
  } else {
    apr_atomic_inc32(&unsuccessfulRequests);
  }

  if (reporting) {
    if (args->latenciesCount >= args->latenciesSize) {
      args->latenciesSize *= 2;
      args->latencies =
	(unsigned long*)realloc(args->latencies, 
				sizeof(unsigned long) * args->latenciesSize);
    }
    args->latencies[args->latenciesCount] = latency;
    args->latenciesCount++;
  }
}

void RecordSocketError(void)
{
  apr_atomic_inc32(&socketErrors);
}

void RecordConnectionOpen(void)
{
  apr_atomic_inc32(&connectionsOpened);
}

void RecordInit(const char* monitorHost, const char* host2)
{
  cpu_Init(MainPool);
  if (monitorHost != NULL) {
    remoteMonitorHost = monitorHost;
  }
  if (host2 != NULL) {
    remote2MonitorHost = host2;
  }
}

void RecordStart(int startReporting)
{
  /* When we warm up we want to zero these out before continuing */
  completedRequests = 0;
  successfulRequests = 0;
  intervalSuccessful = 0;
  unsuccessfulRequests = 0;
  socketErrors = 0;
  connectionsOpened = 0;

  reporting = startReporting;  
  cpu_GetUsage(&cpuUsage, MainPool);
  if (remoteMonitorHost != NULL) {
    if (remoteCpuSocket == NULL) {
      connectMonitor(remoteMonitorHost, &remoteCpuSocket);
    } else {
      /* Just re-set the CPU time */
      getRemoteStat(CPU_CMD, &remoteCpuSocket);
    }
  }
  if (remote2MonitorHost != NULL) {
    if (remote2CpuSocket == NULL) {
      connectMonitor(remote2MonitorHost, &remote2CpuSocket);
    } else {
      /* Just re-set the CPU time */
      getRemoteStat(CPU_CMD, &remote2CpuSocket);
    }
  }
  startTime = apr_time_now();
  intervalStartTime = startTime;

  initSamples(&clientSamples);
  initSamples(&remoteSamples);
  initSamples(&remote2Samples);
}

void RecordStop(void)
{
  clientMem = cpu_GetMemoryUsage(MainPool);
  if (remoteCpuSocket != NULL) {
    remoteMem = getRemoteStat(MEM_CMD, &remoteCpuSocket);
  }
  if (remote2CpuSocket != NULL) {
    remote2Mem = getRemoteStat(MEM_CMD, &remote2CpuSocket);
  }
  reporting = 0;
  stopTime = apr_time_now();
}

void ReportInterval(FILE* out, int totalDuration, int warmup)
{
  double cpu = 0.0;
  double remoteCpu = 0.0;
  double remote2Cpu = 0.0;

  if (!warmup) {
    if (remoteCpuSocket != NULL) {
      remoteCpu = getRemoteStat(CPU_CMD, &remoteCpuSocket);
      addSample(remoteCpu, &remoteSamples);
    }
    if (remote2CpuSocket != NULL) {
      remote2Cpu = getRemoteStat(CPU_CMD, &remote2CpuSocket);
      addSample(remote2Cpu, &remote2Samples);
    }
    cpu = cpu_GetInterval(&cpuUsage, MainPool);
    addSample(cpu, &clientSamples);
  }

  if (ShortOutput) {
    return;
  }

  apr_uint32_t count = intervalSuccessful;
  apr_time_t now = apr_time_now();
  apr_time_t e = now - intervalStartTime;
  int soFar = apr_time_sec(now - startTime);
  double elapsed = microToSecond(e);
  char* warm;

  if (warmup) {
    warm = "Warming up: ";
  } else {
    warm = "";
  }


  intervalSuccessful = 0;
  intervalStartTime = now;
  if (remoteCpu != 0.0) {
  fprintf(out, "%s(%u / %i) %.3lf %.0lf%% cpu %.0lf%% remote cpu\n",
	  warm, soFar, totalDuration, count / elapsed, 
	  cpu * 100.0, remoteCpu * 100.0);
  } else {
    fprintf(out, "%s(%u / %i) %.3lf %.0lf%% cpu\n",
	    warm, soFar, totalDuration, count / elapsed, cpu * 100.0);
  }
}

static int compareULongs(const void* a1, const void* a2)
{
  const unsigned long* l1 = (const unsigned long*)a1;
  const unsigned long* l2 = (const unsigned long*)a2;

  if (*l1 < *l2) {
    return -1;
  } else if (*l1 > *l2) {
    return 1;
  }
  return 0;
}

static int compareDoubles(const void* a1, const void* a2)
{
  const double* d1 = (const double*)a1;
  const double* d2 = (const double*)a2;

  if (*d1 < *d2) {
    return -1;
  } else if (*d1 > *d2) {
    return 1;
  }
  return 0;
}

static unsigned long getLatencyPercent(int percent)
{
  if (latenciesCount == 0) {
    return 0;
  }
  if (percent == 100) {
    return latencies[latenciesCount - 1];
  }
  unsigned int index = 
    (latenciesCount / 100.0) * percent;
  return latencies[index];
}

static unsigned long getAverageLatency(void)
{
  if (latenciesCount == 0) {
    return 0;
  }
  unsigned long long totalLatency = 0LL;

  for (unsigned int i = 0; i < latenciesCount; i++) {
    totalLatency += latencies[i];
  }

  return totalLatency / (unsigned long long)latenciesCount;
}

static double getLatencyStdDev(void)
{
  if (latenciesCount == 0) {
    return 0.0;
  }
  unsigned long avg = microToMilli(getAverageLatency());
  double differences = 0.0;

  for (unsigned int i = 0; i < latenciesCount; i++) {
    differences += pow(microToMilli(latencies[i]) - avg, 2.0);
  }
  
  return sqrt(differences / (double)latenciesCount);
}

static double getAverageCpu(const CPUSamples* s)
{
  if (s->count == 0) {
    return 0.0;
  }
  double total = 0.0;

  for (size_t i = 0; i < s->count; i++) {
    total += s->samples[i];
  }

  return total / s->count;
}

static double getMaxCpu(CPUSamples* s)
{
  if (s->count == 0) {
    return 0.0;
  }
   
  qsort(s->samples, s->count, sizeof(double), compareDoubles);
  return s->samples[s->count - 1];
}

static void PrintNormalResults(FILE* out, double elapsed)
{
  fprintf(out, "Duration:             %.3lf seconds\n", elapsed);
  fprintf(out, "Attempted requests:   %u\n", completedRequests);
  fprintf(out, "Successful requests:  %u\n", successfulRequests);
  fprintf(out, "Non-200 results:      %u\n", unsuccessfulRequests);
  fprintf(out, "Connections opened:   %u\n", connectionsOpened);
  fprintf(out, "Socket errors:        %u\n", socketErrors);
  fprintf(out, "\n");
  fprintf(out, "Throughput:           %.3lf requests/second\n", 
          successfulRequests / elapsed);
  fprintf(out, "Average latency:      %.3lf milliseconds\n",
	  microToMilli(getAverageLatency()));
  fprintf(out, "Minimum latency:      %.3lf milliseconds\n",
	  microToMilli(getLatencyPercent(0)));
  fprintf(out, "Maximum latency:      %.3lf milliseconds\n",
	  microToMilli(getLatencyPercent(100)));
  fprintf(out, "Latency std. dev:     %.3lf milliseconds\n",
          getLatencyStdDev());
  fprintf(out, "50%% latency:          %.3lf milliseconds\n",
	  microToMilli(getLatencyPercent(50)));
  fprintf(out, "90%% latency:          %.3lf milliseconds\n",
	  microToMilli(getLatencyPercent(90)));
  fprintf(out, "98%% latency:          %.3lf milliseconds\n",
	  microToMilli(getLatencyPercent(98)));
  fprintf(out, "99%% latency:          %.3lf milliseconds\n",
	  microToMilli(getLatencyPercent(99)));
  fprintf(out, "\n");
  if (clientSamples.count > 0) {
    fprintf(out, "Client CPU average:    %.0lf%%\n",
	    getAverageCpu(&clientSamples) * 100.0);
    fprintf(out, "Client CPU max:        %.0lf%%\n",
	    getMaxCpu(&clientSamples) * 100.0);
  }
  fprintf(out, "Client memory usage:    %.0lf%%\n",
          clientMem * 100.0);
  if (remoteSamples.count > 0) {
    fprintf(out, "Remote CPU average:    %.0lf%%\n",
	    getAverageCpu(&remoteSamples) * 100.0);
    fprintf(out, "Remote CPU max:        %.0lf%%\n",
	    getMaxCpu(&remoteSamples) * 100.0);
    fprintf(out, "Remote memory usage:   %.0lf%%\n",
            remoteMem * 100.0);
  }
  if (remote2Samples.count > 0) {
    fprintf(out, "Remote 2 CPU average:    %.0lf%%\n",
	    getAverageCpu(&remote2Samples) * 100.0);
    fprintf(out, "Remote 2 CPU max:        %.0lf%%\n",
	    getMaxCpu(&remote2Samples) * 100.0);
    fprintf(out, "Remote 2 memory usage:   %.0lf%%\n",
            remote2Mem * 100.0);
  }
  fprintf(out, "\n");
  fprintf(out, "Total bytes sent:      %.2lf megabytes\n",
	  totalBytesSent / 1048576.0);
  fprintf(out, "Total bytes received:  %.2lf megabytes\n",
	  totalBytesReceived / 1048576.0);
  fprintf(out, "Send bandwidth:        %.2lf megabits / second\n",
	  (totalBytesSent * 8.0 / 1048576.0) / elapsed);
  fprintf(out, "Receive bandwidth:     %.2lf megabits / second\n",
	  (totalBytesReceived * 8.0 / 1048576.0) / elapsed);
}

static void PrintShortResults(FILE* out, double elapsed)
{
  /*
  name,throughput,avg. latency,threads,connections,duration,completed,successful,errors,sockets,min. latency,max. latency,50%,90%,98%,99%
   */
  fprintf(out,
	  "%s,%.3lf,%.3lf,%u,%u,%.3lf,%u,%u,%u,%u,%.3lf,%.3lf,%.3lf,%.3lf,%.3lf,%.3lf,%.3lf,%.0lf,%.0lf,%.0lf,%.0lf,%.0lf,%.0lf,%.2lf,%.2lf\n",
	  RunName, successfulRequests / elapsed, 
	  microToMilli(getAverageLatency()),
	  NumThreads, NumConnections, elapsed,
	  completedRequests, successfulRequests,
	  unsuccessfulRequests, connectionsOpened,
	  microToMilli(getLatencyPercent(0)), 
          microToMilli(getLatencyPercent(100)),
	  microToMilli(getLatencyPercent(50)),
	  microToMilli(getLatencyPercent(90)),
	  microToMilli(getLatencyPercent(98)),
	  microToMilli(getLatencyPercent(99)),
          getLatencyStdDev(),
	  getAverageCpu(&clientSamples) * 100.0,
	  getAverageCpu(&remoteSamples) * 100.0,
	  getAverageCpu(&remote2Samples) * 100.0,
          clientMem * 100.0,
          remoteMem * 100.0,
          remote2Mem * 100.0,
	  (totalBytesSent * 8.0 / 1048576.0) / elapsed,
	  (totalBytesReceived * 8.0 / 1048576.0) / elapsed);
}

void PrintReportingHeader(FILE* out)
{
  fprintf(out, "Name,Throughput,Avg. Latency,Threads,Connections,Duration," \
          "Completed,Successful,Errors,Sockets," \
          "Min. latency,Max. latency,50%% Latency,90%% Latency,"\
          "98%% Latency,99%% Latency,Latency Std Dev,Avg Client CPU,"\
          "Avg Server CPU,Avg Server 2 CPU,"\
          "Client Mem Usage,Server Mem,Server 2 Mem,"\
	  "Avg. Send Bandwidth,Avg. Recv. Bandwidth\n");
}

void PrintResults(FILE* out)
{
  apr_time_t e = stopTime - startTime;
  double elapsed = microToSecond(e);

  if (ShortOutput) {
    PrintShortResults(out, elapsed);
  } else {
    PrintNormalResults(out, elapsed);
  }
}


void ConsolidateLatencies(IOArgs* args, int numThreads)
{
  unsigned int latenciesSize = args[0].latenciesSize;

  if (args == NULL) {
    return;
  }

  latencies = (unsigned long*)malloc(sizeof(unsigned long) * latenciesSize);

  for (int i = 0; i < numThreads; i++) {
    if ((latenciesCount + args[i].latenciesCount) >= latenciesSize) {
      latenciesSize *= 2;
      if ((latenciesCount + args[i].latenciesCount) >= latenciesSize) {
	latenciesSize += args[i].latenciesCount;
      }
      latencies = (unsigned long*)realloc(latencies,
					 sizeof(unsigned long) * latenciesSize);
    }
    memcpy(latencies + latenciesCount, args[i].latencies, 
	   sizeof(unsigned long) * args[i].latenciesCount);
    latenciesCount += args[i].latenciesCount;
    free(args[i].latencies);

    totalBytesSent += args[i].writeBytes;
    totalBytesReceived += args[i].readBytes;
  }

  qsort(latencies, latenciesCount, sizeof(unsigned long),
	compareULongs);
}

void EndReporting(void)
{
  if (remoteCpuSocket != NULL) {
    apr_socket_close(remoteCpuSocket);
  }
  if (remote2CpuSocket != NULL) {
    apr_socket_close(remote2CpuSocket);
  }
  if (latencies != NULL) {
    free(latencies);
    freeSamples(&clientSamples);
    freeSamples(&remoteSamples);
    freeSamples(&remote2Samples);
  }
}
