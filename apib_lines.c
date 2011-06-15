#include <apib_common.h>

static int isChar(const char c, const char* match)
{
  unsigned int m = 0;

  while (match[m] != 0) {
    if (c == match[m]) {
      return 1;
    }
    m++;
  }
  return 0;
}

void linep_Start(LineState* l, char* line, unsigned int size,
		 unsigned int len)
{
  l->httpMode = 0;
  l->buf = line;
  l->bufSize = size;
  l->bufLen = len;
  l->lineStart = l->lineEnd = 0;
  l->tokStart = l->tokEnd = 0;
  l->lineComplete = 0;
}

void linep_SetHttpMode(LineState* l, int on)
{
  l->httpMode = on;
}

static void nullLast(LineState* l) 
{
  l->buf[l->lineEnd] = 0;
  l->lineEnd++;
}

int linep_NextLine(LineState* l)
{
  if (l->lineEnd > 0) {
    l->lineStart = l->lineEnd;
  }
  if (l->lineEnd >= l->bufLen) {
    l->lineComplete = 0;
    return 0;
  }
  
  /* Move to the first newline character */
  while ((l->lineEnd < l->bufLen) &&
	 !isChar(l->buf[l->lineEnd], "\r\n")) {
    l->lineEnd++;
  }
  if (l->lineEnd == l->bufLen) {
    /* Incomplete line in the buffer */
    l->lineComplete = 0;
    return 0;
  }

  if (l->httpMode) {
    if (l->buf[l->lineEnd] == '\r') {
      nullLast(l);
      if (l->buf[l->lineEnd] == '\n') {
	nullLast(l);
      }
    } else {
      nullLast(l);
    }      
  } else {
    /* Overwrite all newlines with nulls */
    while ((l->lineEnd < l->bufLen) &&
	   isChar(l->buf[l->lineEnd], "\r\n")) {
      nullLast(l);
    }
  }

  l->tokStart = l->tokEnd = l->lineStart;

  l->lineComplete = 1;
  return 1;
}

char* linep_GetLine(LineState* l)
{
  if (!l->lineComplete) {
    return NULL;
  }
  return l->buf + l->lineStart;
}

char* linep_NextToken(LineState* l, const char* toks)
{
  if (!l->lineComplete) {
    return NULL;
  }
  if (l->tokEnd >= l->lineEnd) {
    return NULL;
  }

  l->tokStart = l->tokEnd;

  while ((l->tokEnd < l->lineEnd) &&
	 !isChar(l->buf[l->tokEnd], toks)) {
    l->tokEnd++;
  }
  while ((l->tokEnd < l->lineEnd) &&
	 isChar(l->buf[l->tokEnd], toks)) {
    l->buf[l->tokEnd] = 0;
    l->tokEnd++;
  }

  return l->buf + l->tokStart;
}

int linep_Reset(LineState* l)
{
  unsigned int remaining;
  if (!l->lineComplete) {
    remaining = l->bufLen - l->lineStart;
    memmove(l->buf, l->buf + l->lineStart, remaining);
  } else {
    remaining = 0;
  }
  l->bufLen = remaining;
  l->lineStart = l->lineEnd = 0;
  l->lineComplete = 0;
  return (remaining >= l->bufSize);
}

int linep_ReadFile(LineState* l, apr_file_t* file)
{
  unsigned int len = l->bufSize - l->bufLen;
  apr_status_t s;

  s = apr_file_read(file, l->buf + l->bufLen, &len);
  l->bufLen += len;
  return s;
}

int linep_ReadSocket(LineState* l, apr_socket_t* sock)
{
  unsigned int len = l->bufSize - l->bufLen;
  apr_status_t s;

  s = apr_socket_recv(sock, l->buf + l->bufLen, &len);
  l->bufLen += len;
  return s;
}

void linep_GetReadInfo(const LineState* l, char** buf, 
		       unsigned int* remaining)
{
  if (buf != NULL) {
    *buf = l->buf + l->bufLen;
  }
  if (remaining != NULL) {
    *remaining = l->bufSize - l->bufLen;
  }
}

void linep_GetDataRemaining(const LineState* l, unsigned int* remaining)
{
  *remaining = l->bufLen - l->lineEnd;
}

void linep_Skip(LineState* l, unsigned int toSkip)
{
  l->lineEnd += toSkip;
}

void linep_SetReadLength(LineState* l, unsigned int len)
{
  l->bufLen += len;
}

void linep_Debug(const LineState* l, FILE* out)
{
  fprintf(out, "buf len = %u line start = %u end = %u tok start = %u end = %u\n",
	  l->bufLen, l->lineStart, l->lineEnd, 
	  l->tokStart, l->tokEnd);
}
