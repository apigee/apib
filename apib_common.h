#ifndef APIB_COMMON_H
#define APIB_COMMON_H

#include <apr_file_io.h>

typedef struct {
  char*           buf;
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

/* Read the first complete line -- return 0 if a complete line is not present. */
extern int linep_NextLine(LineState* l);

/* If NextLine returned non-zero, return a pointer to the entire line */
extern char* linep_GetLine(LineState* l);

/* If NextLine returned non-zero, return the next token delimited by "toks" like strtok */
extern char* linep_NextToken(LineState* l, const char* toks);

/* Move any data remaining in the line to the start. Used if we didn't
   read a complete line and are still expecting more data. 
   Return the starting position for the buffer for the next read */
extern unsigned int linep_Reset(LineState* l);

/* Fill the line buffer with data from a file */
extern int linep_ReadFile(LineState* l, apr_file_t* file);

#endif
