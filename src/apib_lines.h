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

#ifndef APIB_LINEP_H
#define APIB_LINEP_H

#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Code for managing line-oriented input, that must be broken into lines,
 * then tokenized, and which might come a little bit at a time.
 */

typedef struct {
  char*    buf;
  int      httpMode; /* Lines are terminated by only a single CRLF. */
  int      bufLen; /* The number of valid bytes in the buffer */
  int      bufSize; /* Number of allocated bytes in case it is different */
  int      lineStart;
  int      lineEnd;
  int      lineComplete;
  int      tokStart;
  int      tokEnd;
} LineState;

/* Initialize or reset a LineState with new data. "size" is the size
 * of the buffer, and "len" is the amount that's currently filled with real stuff */
extern void linep_Start(LineState* l, char* line, int size,
			int len);

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
extern int linep_ReadFile(LineState* l, FILE* file);

#if 0
/* Fill the buffer with data from a socket */
extern int linep_ReadSocket(LineState* l, apr_socket_t* sock);
#endif

/* Get info to fill the rest of the buffer */
extern void linep_GetReadInfo(const LineState* l, char** buf, 
			      int* remaining);

/* Find out how much data is left unprocessed */
extern int linep_GetDataRemaining(const LineState* l);

/* Write data from the end of the last line to the end of the buffer */
extern void linep_WriteRemaining(const LineState* l, FILE* out);

/* Skip forward to see if there's another line */
extern void linep_Skip(LineState* l, int toSkip);

/* Report back how much we read */
extern void linep_SetReadLength(LineState* l, int len);

extern void linep_Debug(const LineState* l, FILE* out);

#ifdef __cplusplus
}
#endif

#endif  // APIB_LINEP_H
