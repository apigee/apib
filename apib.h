#ifndef APIB_H
#define APIB_H

#include <stdio.h>

#include <apr_pools.h>
#include <apr_uri.h>

/* Structures */

typedef struct {
  const apr_uri_t* url;
  int             numConnections;
  int             verbose;
  char*           httpVerb;
  char*           contentType;
  char*           sendData;
  unsigned int    sendDataSize;
  unsigned long*  latencies;
  unsigned int    latenciesSize;
  unsigned int    latenciesCount;
  unsigned long   readCount;
  unsigned long   writeCount;
} IOArgs;

/* Globals */

#define USER_AGENT "apib 0.1"

#define DEFAULT_CONTENT_TYPE "application/octet-stream"

extern apr_pool_t* MainPool;
extern volatile int Running;

/* Internal methods */

extern void RunIO(IOArgs* args);

extern void RecordStart(int startReporting);
extern void RecordStop(void);

extern void RecordResult(IOArgs* args, int code, unsigned long latency);
extern void RecordSocketError(void);
extern void RecordConnectionOpen(void);

extern void ConsolidateLatencies(IOArgs* args, int numThreads);
extern void PrintResults(FILE* out);
extern void ReportInterval(FILE* out, int totalDuration, int warmup);


#endif
