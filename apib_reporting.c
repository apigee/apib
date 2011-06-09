#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <apr_atomic.h>
#include <apr_time.h>

#include <apib.h>

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

static double microToMilli(unsigned long m)
{
  return (double)(m / 1000l) + ((m % 1000l) / 1000.0);
}

static double microToSecond(unsigned long m)
{
  return (double)(m / 1000000l) + ((m % 1000000l) / 1000000.0);
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
  startTime = apr_time_now();
  intervalStartTime = startTime;
}

void RecordStop(void)
{
  reporting = 0;
  stopTime = apr_time_now();
}

void ReportInterval(FILE* out, int totalDuration, int warmup)
{
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
  fprintf(out, "%s(%u / %i) %.3lf\n",
	  warm, soFar, totalDuration, count / elapsed);
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
}

static void PrintShortResults(FILE* out, double elapsed)
{
  /*
  name,throughput,avg. latency,threads,connections,duration,completed,successful,errors,sockets,min. latency,max. latency,50%,90%,98%,99%
   */
  fprintf(out,
	  "%s,%.3lf,%.3lf,%u,%u,%.3lf,%u,%u,%u,%u,%.3lf,%.3lf,%.3lf,%.3lf,%.3lf,%.3lf\n",
	  RunName, successfulRequests / elapsed, 
	  microToMilli(getAverageLatency()),
	  NumThreads, NumConnections, elapsed,
	  completedRequests, successfulRequests,
	  unsuccessfulRequests, connectionsOpened,
	  microToMilli(latencies[0]), microToMilli(latencies[latenciesCount - 1]),
	  microToMilli(getLatencyPercent(50)),
	  microToMilli(getLatencyPercent(90)),
	  microToMilli(getLatencyPercent(98)),
	  microToMilli(getLatencyPercent(99)));
}

void PrintReportingHeader(FILE* out)
{
  fprintf(out, "Name,Throughput,Avg. Latency,Threads,Connections,Duration," \
          "Completed,Successful,Errors,Sockets," \
          "Min. latency,Max. latency,50%% Latency,90%% Latency,"\
          "98%% Latency,99%% Latency\n");
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
  }

  qsort(latencies, latenciesCount, sizeof(unsigned long),
	compareULongs);
}

void EndReporting(void)
{
  if (latencies != NULL) {
    free(latencies);
  }
}
