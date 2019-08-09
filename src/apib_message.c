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

#include "src/apib_message.h"

#include <assert.h>
#include <regex.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

#define REQUEST_LINE_EXP "^([a-zA-Z]+) ([^ ]+) HTTP/([0-9])\\.([0-9])$"
#define REQUEST_LINE_PARTS 5
#define STATUS_LINE_EXP "^HTTP/([0-9])\\.([0-9]) ([0-9]+) .*$"
#define STATUS_LINE_PARTS 4
#define HEADER_LINE_EXP "^([^:]+):([ \\t]+)?(.*)$"
#define HEADER_LINE_PARTS 4

static regex_t requestLineRegex;
static regex_t statusLineRegex;
static regex_t headerLineRegex;
static int initialized = 0;

void message_Init() {
  if (!initialized) {
    int s = regcomp(&requestLineRegex, REQUEST_LINE_EXP, REG_EXTENDED);
    assert(s == 0);
    s = regcomp(&statusLineRegex, STATUS_LINE_EXP, REG_EXTENDED);
    assert(s == 0);
    s = regcomp(&headerLineRegex, HEADER_LINE_EXP, REG_EXTENDED);
    assert(s == 0);
    initialized = 1;
  }
}

HttpMessage* message_NewResponse() {
  HttpMessage* r = (HttpMessage*)malloc(sizeof(HttpMessage));
  r->type = Response;
  r->state = MESSAGE_INIT;
  r->statusCode = -1;
  r->majorVersion = -1;
  r->minorVersion = -1;
  r->contentLength = -1;
  r->chunked = -1;
  r->shouldClose = -1;
  r->bodyLength = 0;
  r->chunkState = CHUNK_INIT;
  r->method = NULL;
  r->path = NULL;
  return r;
}

HttpMessage* message_NewRequest() {
  HttpMessage* r = (HttpMessage*)malloc(sizeof(HttpMessage));
  r->type = Request;
  r->state = MESSAGE_INIT;
  r->statusCode = -1;
  r->majorVersion = -1;
  r->minorVersion = -1;
  r->contentLength = -1;
  r->chunked = -1;
  r->shouldClose = -1;
  r->bodyLength = 0;
  r->chunkState = CHUNK_INIT;
  r->method = NULL;
  r->path = NULL;
  return r;
}

void message_Free(HttpMessage* r) {
  if (r->method) {
    free(r->method);
  }
  if (r->path) {
    free(r->path);
  }
  free(r);
}

static char* getPart(const char* s, const regmatch_t* matches, const int ix) {
  const regmatch_t* match = &(matches[ix]);
  if (match->rm_so < 0) {
    return NULL;
  }
  assert(match->rm_eo >= match->rm_so);
  return strndup(s + match->rm_so, match->rm_eo - match->rm_so);
}

static int getIntPart(const char* s, const regmatch_t* matches, const int ix) {
  char* ps = getPart(s, matches, ix);
  if (ps == NULL) {
    return 0;
  }
  int val = atoi(ps);
  free(ps);
  return val;
}

static int comparePart(const char* expected, const char* s,
                       const regmatch_t* matches, const int ix) {
  const regmatch_t* match = &(matches[ix]);
  if (match->rm_so < 0) {
    return -1;
  }
  assert(match->rm_eo >= match->rm_so);
  return strncasecmp(expected, s + match->rm_so, match->rm_eo - match->rm_so);
}

// These functions return an int:
// < 0 : Error
// 0: Success
// > 0: Not enough data -- call again with a more full buffer

static int parseStatus(HttpMessage* r, LineState* buf) {
  if (!linep_NextLine(buf)) {
    // no status line yet
    return 1;
  }

  const char* sl = linep_GetLine(buf);
  regmatch_t matches[STATUS_LINE_PARTS];
  const int rs = regexec(&statusLineRegex, sl, STATUS_LINE_PARTS, matches, 0);
  if (rs != 0) {
    return -1;
  }

  // 1 and 2 are major and minor version numbers
  r->majorVersion = getIntPart(sl, matches, 1);
  r->minorVersion = getIntPart(sl, matches, 2);
  // 3 is the actual status code
  r->statusCode = getIntPart(sl, matches, 3);

  r->state = MESSAGE_STATUS;
  return 0;
}

static int parseRequestLine(HttpMessage* r, LineState* buf) {
  if (!linep_NextLine(buf)) {
    // no request line yet
    return 1;
  }

  const char* rl = linep_GetLine(buf);
  regmatch_t matches[REQUEST_LINE_PARTS];
  const int rs = regexec(&requestLineRegex, rl, REQUEST_LINE_PARTS, matches, 0);
  if (rs != 0) {
    return -1;
  }

  // 1 and 2 are method and path
  r->method = getPart(rl, matches, 1);
  r->path = getPart(rl, matches, 2);
  // 3 and 4 are version numbers
  r->majorVersion = getIntPart(rl, matches, 3);
  r->minorVersion = getIntPart(rl, matches, 4);

  r->state = MESSAGE_STATUS;
  return 0;
}

static void finishHeaders(HttpMessage* r) {
  if ((r->contentLength >= 0) && (r->chunked < 0)) {
    r->chunked = 0;
  } else if ((r->contentLength < 0) && (r->chunked < 0)) {
    r->chunked = 0;
    r->contentLength = 0;
  }
  if (r->shouldClose < 0) {
    r->shouldClose = 0;
  }

  if (r->contentLength == 0) {
    r->state = MESSAGE_DONE;
  } else {
    r->state = MESSAGE_HEADERS;
  }
}

static void examineHeader(HttpMessage* r, const char* line,
                          const regmatch_t* matches) {
  if (!comparePart("Content-Length", line, matches, 1)) {
    r->contentLength = getIntPart(line, matches, 3);
  } else if (!comparePart("Transfer-Encoding", line, matches, 1)) {
    r->chunked = !comparePart("chunked", line, matches, 3);
  } else if (!comparePart("Connection", line, matches, 1)) {
    r->shouldClose = !comparePart("close", line, matches, 3);
  }
}

static int parseHeaderLine(HttpMessage* r, LineState* buf) {
  if (!linep_NextLine(buf)) {
    return 1;
  }

  const char* hl = linep_GetLine(buf);
  if (hl[0] == 0) {
    // Empty line -- means end of headers!
    finishHeaders(r);
    return 0;
  }
  if ((hl[0] == ' ') || (hl[0] == '\t')) {
    // "obs-fold" from RFC7230 section 3.2.4. Just ignore this line.
    return 0;
  }

  regmatch_t matches[HEADER_LINE_PARTS];
  const int rs = regexec(&headerLineRegex, hl, HEADER_LINE_PARTS, matches, 0);
  if (rs != 0) {
    printf("Invalid header line: \"%s\"\n", hl);
    return -2;
  }

  examineHeader(r, hl, matches);
  return 0;
}

static int parseLengthBody(HttpMessage* r, LineState* buf) {
  assert(!r->chunked);
  assert(r->contentLength >= 0);

  const int bufLeft = linep_GetDataRemaining(buf);
  if (bufLeft == 0) {
    // Done with what we have -- we need more!
    return 1;
  }

  const int toRead = r->contentLength - r->bodyLength;
  if (bufLeft <= toRead) {
    r->bodyLength += bufLeft;
    linep_Skip(buf, bufLeft);
  } else {
    r->bodyLength += toRead;
    linep_Skip(buf, toRead);
  }

  if (r->bodyLength == r->contentLength) {
    // Regular message bodies have no trailers
    r->state = MESSAGE_DONE;
  }
  return 0;
}

static int parseChunkHeader(HttpMessage* r, LineState* buf) {
  if (!linep_NextLine(buf)) {
    return 1;
  }

  // TODO regular expression for extra chunk header stuff
  const char* hl = linep_GetLine(buf);
  const int len = strtol(hl, NULL, 16);
  if (len == 0) {
    r->chunkState = CHUNK_END;
  } else {
    r->chunkLength = len;
    r->chunkPosition = 0;
    r->chunkState = CHUNK_LENGTH;
  }
  return 0;
}

static int parseChunkBody(HttpMessage* r, LineState* buf) {
  const int bufLeft = linep_GetDataRemaining(buf);
  if (bufLeft == 0) {
    // Done with what we have -- we need more!
    return 1;
  }

  const int toRead = r->chunkLength - r->chunkPosition;
  if (bufLeft <= toRead) {
    r->chunkPosition += bufLeft;
    linep_Skip(buf, bufLeft);
  } else {
    r->chunkPosition += toRead;
    linep_Skip(buf, toRead);
  }

  if (r->chunkLength == r->chunkPosition) {
    r->bodyLength += r->chunkLength;
    r->chunkState = CHUNK_CHUNK;
  }
  return 0;
}

static int parseChunkEnd(HttpMessage* r, LineState* buf) {
  if (!linep_NextLine(buf)) {
    return 1;
  }
  // Expecting a blank line -- just CRLF
  const char* hl = linep_GetLine(buf);
  if (hl[0] != 0) {
    return -3;
  }
  r->chunkState = CHUNK_INIT;
  return 0;
}

static int parseTrailerLine(HttpMessage* r, LineState* buf) {
  if (!linep_NextLine(buf)) {
    return 1;
  }

  const char* hl = linep_GetLine(buf);
  if (hl[0] == 0) {
    // Empty line -- means end of everything!
    r->state = MESSAGE_DONE;
    return 0;
  }
  if ((hl[0] == ' ') || (hl[0] == '\t')) {
    // "obs-fold" from RFC7230 section 3.2.4. Just ignore this line.
    return 0;
  }

  regmatch_t matches[HEADER_LINE_PARTS];
  const int rs = regexec(&headerLineRegex, hl, HEADER_LINE_PARTS, matches, 0);
  if (rs != 0) {
    printf("Invalid header line: \"%s\"\n", hl);
    return -2;
  }
  // We don't process trailer lines right now.
  return 0;
}

static int fillChunk(HttpMessage* r, LineState* buf) {
  assert(r->chunked);
  for (;;) {
    int s;
    switch (r->chunkState) {
      case CHUNK_INIT:
        s = parseChunkHeader(r, buf);
        break;
      case CHUNK_LENGTH:
        s = parseChunkBody(r, buf);
        break;
      case CHUNK_CHUNK:
        s = parseChunkEnd(r, buf);
        break;
      case CHUNK_END:
        r->state = MESSAGE_BODY;
        return 0;
    }

    if (s < 0) {
      // Parsing error
      return s;
    }
    if (s > 0) {
      // Not enough data to exit current state -- don't continue
      return 1;
    }
    // Otherwise, loop to next state.
  }
}

int message_Fill(HttpMessage* r, LineState* buf) {
  for (;;) {
    int s;
    switch (r->state) {
      case MESSAGE_INIT:
        switch (r->type) {
          case Request:
            s = parseRequestLine(r, buf);
            break;
          case Response:
            s = parseStatus(r, buf);
            break;
          default:
            assert(0);
        }
        break;
      case MESSAGE_STATUS:
        s = parseHeaderLine(r, buf);
        break;
      case MESSAGE_HEADERS:
        if (!r->chunked) {
          s = parseLengthBody(r, buf);
        } else {
          s = fillChunk(r, buf);
        }
        break;
      case MESSAGE_BODY:
        s = parseTrailerLine(r, buf);
        break;
      case MESSAGE_DONE:
        return 0;
      default:
        assert(0);
    }

    if (s < 0) {
      // Parsing error
      return s;
    }
    if (s > 0) {
      // Not enough data to exit current state -- don't continue
      return 0;
    }
    // Otherwise, loop to next state.
  }
}
