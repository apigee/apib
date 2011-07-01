#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <apr_atomic.h>
#include <apr_network_io.h>
#include <apr_time.h>

#include <apib.h>

#define NUM_CPU_SAMPLES 32

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

static double* clientCpuSamples = NULL;
static unsigned int clientCpuSampleCount = 0;
static unsigned int clientCpuSampleSize = 0;
static CPUUsage cpuUsage;

static double* remoteCpuSamples = NULL;
static unsigned int remoteCpuSampleCount = 0;
static unsigned int remoteCpuSampleSize = 0;
static apr_socket_t* remoteCpuSocket = NULL;
static const char* remoteMonitorHost = NULL;

static unsigned long long totalBytesSent = 0LL;
static unsigned long long totalBytesReceived = 0LL;

static double* addSample(double sample, double* samples,
			 unsigned int* count, unsigned int* size)
{
  double* ret = samples;

  if (*count >= *size) {
    *size *= 2;
    ret = (double*)realloc(samples, sizeof(double) * *size);
  }
  ret[*count] = sample;
  (*count)++;
  return ret;
}

static double microToMilli(unsigned long m)
{
  return (double)(m / 1000l) + ((m % 1000l) / 1000.0);
}

static double microToSecond(unsigned long m)
{
  return (double)(m / 1000000l) + ((m % 1000000l) / 1000000.0);
}

static void connectMonitor(void)
{
  apr_status_t s;
  apr_sockaddr_t* addr;
  char* host;
  char* scope;
  apr_port_t port;

  s = apr_parse_addr_port(&host, &scope, &port, remoteMonitorHost, MainPool);
  if (s != APR_SUCCESS) {
    return;
  }

  s = apr_sockaddr_info_get(&addr, host, APR_INET, port, 0, MainPool);
  if (s != APR_SUCCESS) {
    return;
  }

  s = apr_socket_create(&remoteCpuSocket, APR_INET, 
			SOCK_STREAM, APR_PROTO_TCP, MainPool);
  if (s != APR_SUCCESS) {
    return;
  }

  s = apr_socket_connect(remoteCpuSocket, addr);
  if (s != APR_SUCCESS) {
    remoteCpuSocket = NULL;
  }
}

static double getRemoteCpu(void)
{
  char buf[64];
  apr_status_t s;
  apr_size_t len;

  len = 4;
  s = apr_socket_send(remoteCpuSocket, "CPU\n", &len);
  if (s != APR_SUCCESS) {
    goto failure;
  }

  len = 64;
  s = apr_socket_recv(remoteCpuSocket, buf, &len);
  if (s != APR_SUCCESS) {
    goto failure;
  }

  return strtod(buf, NULL);

 failure:
  apr_socket_close(remoteCpuSocket);
  remoteCpuSocket = NULL;
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

void RecordInit(const char* monitorHost)
{
  cpu_Init(MainPool);
  if (monitorHost != NULL) {
    remoteMonitorHost = monitorHost;
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
      connectMonitor();
    } else {
      /* Just re-set the CPU time */
      getRemoteCpu();
    }
  }
  startTime = apr_time_now();
  intervalStartTime = startTime;

  clientCpuSampleSize = NUM_CPU_SAMPLES;
  clientCpuSamples = (double*)malloc(sizeof(double) * NUM_CPU_SAMPLES);
  remoteCpuSampleSize = NUM_CPU_SAMPLES;
  remoteCpuSamples = (double*)malloc(sizeof(double) * NUM_CPU_SAMPLES);
}

void RecordStop(void)
{
  reporting = 0;
  stopTime = apr_time_now();
}

void ReportInterval(FILE* out, int totalDuration, int warmup)
{
  double cpu = 0.0;
  double remoteCpu = 0.0;

  if (!warmup) {
    if (remoteCpuSocket != NULL) {
      remoteCpu = getRemoteCpu();
      remoteCpuSamples = addSample(remoteCpu, remoteCpuSamples,
				   &remoteCpuSampleCount, &remoteCpuSampleSize);
    }
    cpu = cpu_GetInterval(&cpuUsage, MainPool);
    clientCpuSamples = addSample(cpu, clientCpuSamples,
				 &clientCpuSampleCount, &clientCpuSampleSize);
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

static unsigned long getLatencyPercent(int percent)
{
  unsigned int index = 
    (latenciesCount / 100) * percent;
  return latencies[index];
}

static unsigned long getAverageLatency(void)
{
  unsigned long long totalLatency = 0LL;

  for (unsigned int i = 0; i < latenciesCount; i++) {
    totalLatency += latencies[i];
  }

  return totalLatency / (unsigned long long)latenciesCount;
}

static double getAverageCpu(double* samples, unsigned int count)
{
  if (count == 0) {
	return 0.0;
  }
  double total = 0.0;

  for (unsigned int i = 0; i < count; i++) {
    total += samples[i];
  }

  return total / count;
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
	  microToMilli(latencies[0]));
  fprintf(out, "Maximum latency:      %.3lf milliseconds\n",
	  microToMilli(latencies[latenciesCount - 1]));
  fprintf(out, "50%% latency:          %.3lf milliseconds\n",
	  microToMilli(getLatencyPercent(50)));
  fprintf(out, "90%% latency:          %.3lf milliseconds\n",
	  microToMilli(getLatencyPercent(90)));
  fprintf(out, "98%% latency:          %.3lf milliseconds\n",
	  microToMilli(getLatencyPercent(98)));
  fprintf(out, "99%% latency:          %.3lf milliseconds\n",
	  microToMilli(getLatencyPercent(99)));
  fprintf(out, "\n");
  if (clientCpuSampleCount > 0) {
    fprintf(out, "Client CPU average:    %.0lf%%\n",
	    getAverageCpu(clientCpuSamples, clientCpuSampleCount) * 100.0);
    fprintf(out, "Client CPU max:        %.0lf%%\n",
	    clientCpuSamples[clientCpuSampleCount - 1] * 100.0);
  }
  if (remoteCpuSampleCount > 0) {
    fprintf(out, "Remote CPU average:    %.0lf%%\n",
	    getAverageCpu(remoteCpuSamples, remoteCpuSampleCount) * 100.0);
    fprintf(out, "Remote CPU max:        %.0lf%%\n",
	    remoteCpuSamples[remoteCpuSampleCount - 1] * 100.0);
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
	  "%s,%.3lf,%.3lf,%u,%u,%.3lf,%u,%u,%u,%u,%.3lf,%.3lf,%.3lf,%.3lf,%.3lf,%.3lf,%.0lf,%.0lf\n",
	  RunName, successfulRequests / elapsed, 
	  microToMilli(getAverageLatency()),
	  NumThreads, NumConnections, elapsed,
	  completedRequests, successfulRequests,
	  unsuccessfulRequests, connectionsOpened,
	  microToMilli(latencies[0]), microToMilli(latencies[latenciesCount - 1]),
	  microToMilli(getLatencyPercent(50)),
	  microToMilli(getLatencyPercent(90)),
	  microToMilli(getLatencyPercent(98)),
	  microToMilli(getLatencyPercent(99)),
	  getAverageCpu(clientCpuSamples, clientCpuSampleCount) * 100.0,
	  getAverageCpu(remoteCpuSamples, remoteCpuSampleCount) * 100.0);
}

void PrintReportingHeader(FILE* out)
{
  fprintf(out, "Name,Throughput,Avg. Latency,Threads,Connections,Duration," \
          "Completed,Successful,Errors,Sockets," \
          "Min. latency,Max. latency,50%% Latency,90%% Latency,"\
          "98%% Latency,99%% Latency,Avg Client CPU,Avg Server CPU\n");
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

  qsort(clientCpuSamples, clientCpuSampleCount, sizeof(double),
	compareDoubles);
  qsort(remoteCpuSamples, remoteCpuSampleCount, sizeof(double),
	compareDoubles);
}

void EndReporting(void)
{
  if (remoteCpuSocket != NULL) {
    apr_socket_close(remoteCpuSocket);
  }
  if (latencies != NULL) {
    free(latencies);
    free(clientCpuSamples);
    if (remoteCpuSamples != NULL) {
      free(remoteCpuSamples);
    }
  }
}
