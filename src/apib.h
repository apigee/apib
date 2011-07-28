#ifndef APIB_H
#define APIB_H

#include <config.h>

#include <stdio.h>

#include <apr_pools.h>
#include <apr_uri.h>

#include <openssl/ssl.h>

#include <apib_common.h>

/* Structures */

typedef struct {
  apr_pool_t*     pool;
  const apr_uri_t* url;
  int             numConnections;
  int             keepRunning;
  int             verbose;
  char*           httpVerb;
  char*           contentType;
  char*           sendData;
  unsigned int    sendDataSize;
  SSL_CTX*        sslCtx;
  char**          headers;
  pq_Queue*       delayQueue;
  unsigned int    thinkTime;
  unsigned int    numHeaders;
  unsigned long*  latencies;
  unsigned int    latenciesSize;
  unsigned int    latenciesCount;
  unsigned long   readCount;
  unsigned long   writeCount;
  unsigned long long readBytes;
  unsigned long long writeBytes;
} IOArgs;

/* Globals */

#define USER_AGENT "apib 0.1"

#define DEFAULT_CONTENT_TYPE "application/octet-stream"
#define KEEP_ALIVE_ALWAYS -1
#define KEEP_ALIVE_NEVER 0

extern apr_pool_t*     MainPool;
extern volatile int    Running;
extern int             NumConnections;
extern int             NumThreads;
extern int             JustOnce;
extern int             ShortOutput;
extern char*           RunName;
extern int             KeepAlive;

extern char*           OAuthCK;
extern char*           OAuthCS;
extern char*           OAuthAT;
extern char*           OAuthAS;

/* Internal methods */

extern void RunIO(IOArgs* args);

extern void RecordInit(const char* monitorHost, const char* monitor2Host);
extern void RecordStart(int startReporting);
extern void RecordStop(void);

extern void RecordResult(IOArgs* args, int code, unsigned long latency);
extern void RecordSocketError(void);
extern void RecordConnectionOpen(void);

extern void ConsolidateLatencies(IOArgs* args, int numThreads);
extern void PrintResults(FILE* out);
extern void ReportInterval(FILE* out, int totalDuration, int warmup);

extern void PrintReportingHeader(FILE* out);

extern void EndReporting(void);

#endif
