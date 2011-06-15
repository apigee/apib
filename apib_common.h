#ifndef APIB_COMMON_H
#define APIB_COMMON_H

#include <stdio.h>

#include <apr_file_io.h>
#include <apr_network_io.h>
#include <apr_pools.h>

/*
 * Code for managing CPU information.
 */

typedef struct {
  long long user;
  long long nice;
  long long system;
  long long idle;
  long long ioWait;
  apr_time_t timestamp;
} CPUUsage;

/* Return the number of CPUs we have, or 1 if we're unable to determine */
extern int cpu_Count(apr_pool_t* pool);

/* Initialize the CPU library. */
extern void cpu_Init(apr_pool_t* pool);

/* Copy current CPU usage to the CPUUsage object. */
extern void cpu_GetUsage(CPUUsage* usage, apr_pool_t* pool);

/* Get CPU usage data for the interval since we last called this method.
   Result is a ratio (between 0 and 1.0) of CPU used by user + nice + system. 
   Usage is across all CPUs (we counted the CPUs before).
   "usage" must be initialized by cpu_GetUsage the first time. 
   Each call after that to cpu_GetInterval copies the current usage to
   "usage". */
extern double cpu_GetInterval(CPUUsage* usage, apr_pool_t* pool);

/*
 * Code for managing line-oriented input, that must be broken into lines,
 * then tokenized, and which might come a little bit at a time.
 */

typedef struct {
  char*           buf;
  int             httpMode; /* Lines are terminated by only a single CRLF. */
  unsigned int    bufLen; /* The number of valid bytes in the buffer */
  unsigned int    bufSize; /* Number of allocated bytes in case it is different */
  unsigned int    lineStart;
  unsigned int    lineEnd;
  int             lineComplete;
  unsigned int    tokStart;
  unsigned int    tokEnd;
} LineState;

/* Initialize or reset a LineState with new data. "size" is the size
 * of the buffer, and "len" is the amount that's currently filled with real stuff */
extern void linep_Start(LineState* l, char* line, unsigned int size,
			unsigned int len);

/* If set to non-zero, then every line is terminated by a single CRLF. Otherwise
   we eat them all up and return no blank lines. Http relies on blank lines! */
extern void linep_SetHttpMode(LineState* l, int on);

/* Read the first complete line -- return 0 if a complete line is not present. */
extern int linep_NextLine(LineState* l);

/* If NextLine returned non-zero, return a pointer to the entire line */
extern char* linep_GetLine(LineState* l);

/* If NextLine returned non-zero, return the next token delimited by "toks" like strtok */
extern char* linep_NextToken(LineState* l, const char* toks);

/* Move any data remaining in the line to the start. Used if we didn't
   read a complete line and are still expecting more data. 
   If we return non-zero, it means that the buffer is full and we don't
   have a complete line. Implementations should know that means that the lines
   are too long and stop processing. */
extern int linep_Reset(LineState* l);

/* Fill the line buffer with data from a file. Return what the read call did. */
extern int linep_ReadFile(LineState* l, apr_file_t* file);

/* Fill the buffer with data from a socket */
extern int linep_ReadSocket(LineState* l, apr_socket_t* sock);

/* Get info to fill the rest of the buffer */
extern void linep_GetReadInfo(const LineState* l, char** buf, 
			      unsigned int* remaining);

/* Find out how much data is left unprocessed */
extern void linep_GetDataRemaining(const LineState* l, unsigned int* remaining);

/* Skip forward to see if there's another line */
extern void linep_Skip(LineState* l, unsigned int toSkip);

/* Report back how much we read */
extern void linep_SetReadLength(LineState* l, unsigned int len);

extern void linep_Debug(const LineState* l, FILE* out);

#endif
