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

#include <cassert>
#include <iostream>
#include <regex>

#include "absl/strings/numbers.h"
#include "src/apib_util.h"

using std::cerr;
using std::endl;

namespace apib {

const int kMessageInit = 0;
const int kMessageStatus = 1;
const int kMessageHeaders = 2;
const int kMessageBody = 3;
const int kMessageDone = 4;

const int kChunkInit = 0;
const int kChunkLength = 1;
const int kChunkChunk = 2;
const int kChunkEnd = 3;

static const std::regex requestLineRegex(
    "^([a-zA-Z]+) ([^ ]+) HTTP/([0-9])\\.([0-9])$");
static const int kRequestLineParts = 5;
static const std::regex statusLineRegex("^HTTP/([0-9])\\.([0-9]) ([0-9]+) .*$");
static const int kStatusLineParts = 4;
static const std::regex headerLineRegex("^([^:]+):([ \\t]+)?(.*)$");
static const int kHeaderLineParts = 4;

HttpMessage::HttpMessage(MessageType t) : type(t) { clear(); }

void HttpMessage::clear() {
  state = kMessageInit;
  minorVersion = majorVersion = -1;
  statusCode = -1;
  method.clear();
  path.clear();
  contentLength = -1;
  chunked = -1;
  shouldClose_ = -1;
  bodyLength = -1;
  chunkState_ = kChunkInit;
  chunkLength_ = -1;
  chunkPosition_ = -1;
}

// These functions return an int:
// < 0 : Error
// 0: Success
// > 0: Not enough data -- call again with a more full buffer

int HttpMessage::parseStatus(LineState* buf) {
  if (!buf->next()) {
    // no status line yet
    return 1;
  }

  const auto line = buf->line();
  std::smatch matches;
  const std::string lines(line);
  if (!std::regex_match(lines, matches, statusLineRegex)) {
    return -1;
  }
  assert(matches.size() == kStatusLineParts);
  majorVersion = std::stol(matches[1]);
  minorVersion = std::stol(matches[2]);
  statusCode = std::stol(matches[3]);

  state = kMessageStatus;
  return 0;
}

int HttpMessage::parseRequestLine(LineState* buf) {
  if (!buf->next()) {
    // no request line yet
    return 1;
  }

  const auto line = buf->line();
  std::smatch matches;
  const std::string lines(line);
  if (!std::regex_match(lines, matches, requestLineRegex)) {
    return -1;
  }
  assert(matches.size() == kRequestLineParts);
  method = matches[1];
  path = matches[2];
  majorVersion = std::stol(matches[3]);
  minorVersion = std::stol(matches[4]);

  state = kMessageStatus;
  return 0;
}

void HttpMessage::finishHeaders() {
  if ((contentLength >= 0) && (chunked < 0)) {
    chunked = 0;
  } else if ((contentLength < 0) && (chunked < 0)) {
    chunked = 0;
    contentLength = 0;
  }
  bodyLength = 0;
  if (shouldClose_ < 0) {
    shouldClose_ = 0;
  }

  if (contentLength == 0) {
    state = kMessageDone;
  } else {
    state = kMessageHeaders;
  }
}

void HttpMessage::examineHeader(const absl::string_view name,
                                const absl::string_view value) {
  if (eqcase("Content-Length", name)) {
    int32_t newLength;
    if (absl::SimpleAtoi(value, &newLength)) {
      contentLength = newLength;
    }
  } else if (eqcase("Transfer-Encoding", name)) {
    chunked = eqcase("chunked", value) ? 1 : 0;
  } else if (eqcase("Connection", name)) {
    shouldClose_ = eqcase("close", value) ? 1 : 0;
  }
}

int HttpMessage::parseHeaderLine(LineState* buf) {
  if (!buf->next()) {
    return 1;
  }

  const auto hl = buf->line();
  if (hl.empty()) {
    // Empty line -- means end of headers!
    finishHeaders();
    return 0;
  }
  if ((hl[0] == ' ') || (hl[0] == '\t')) {
    // "obs-fold" from RFC7230 section 3.2.4. Just ignore this line.
    return 0;
  }

  /*
    std::smatch matches;
    if (!std::regex_match(hl, matches, headerLineRegex)) {
      cerr << "Invalid header line: \"" << hl << '\"' << endl;
      return -2;
    }
    assert(matches.size() == kHeaderLineParts);
    examineHeader(matches[1], matches[3]);
    */

  const auto name = buf->nextToken(":");
  buf->skipMatches(" \t");
  const auto value = buf->nextToken("");
  examineHeader(name, value);

  return 0;
}

int HttpMessage::parseLengthBody(LineState* buf) {
  assert(!chunked);
  assert(contentLength >= 0);

  const int32_t bufLeft = buf->dataRemaining();
  if (bufLeft == 0) {
    // Done with what we have -- we need more!
    return 1;
  }

  const int32_t toRead = contentLength - bodyLength;
  if (bufLeft <= toRead) {
    bodyLength += bufLeft;
    buf->skip(bufLeft);
  } else {
    bodyLength += toRead;
    buf->skip(toRead);
  }

  if (bodyLength == contentLength) {
    // Regular message bodies have no trailers
    state = kMessageDone;
  }
  return 0;
}

int HttpMessage::parseChunkHeader(LineState* buf) {
  if (!buf->next()) {
    return 1;
  }

  // TODO regular expression for extra chunk header stuff like encoding!
  const long len = std::stol(std::string(buf->line()), 0, 16);
  if (len == 0) {
    chunkState_ = kChunkEnd;
  } else {
    chunkLength_ = len;
    chunkPosition_ = 0;
    chunkState_ = kChunkLength;
  }
  return 0;
}

int HttpMessage::parseChunkBody(LineState* buf) {
  const int bufLeft = buf->dataRemaining();
  if (bufLeft == 0) {
    // Done with what we have -- we need more!
    return 1;
  }

  const int32_t toRead = chunkLength_ - chunkPosition_;
  if (bufLeft <= toRead) {
    chunkPosition_ += bufLeft;
    buf->skip(bufLeft);
  } else {
    chunkPosition_ += toRead;
    buf->skip(toRead);
  }

  if (chunkLength_ == chunkPosition_) {
    bodyLength += chunkLength_;
    chunkState_ = kChunkChunk;
  }
  return 0;
}

int HttpMessage::parseChunkEnd(LineState* buf) {
  if (!buf->next()) {
    return 1;
  }
  // Expecting a blank line -- just CRLF
  if (!buf->line().empty()) {
    return -3;
  }
  chunkState_ = kChunkInit;
  return 0;
}

int HttpMessage::parseTrailerLine(LineState* buf) {
  if (!buf->next()) {
    return 1;
  }

  const auto hl = buf->line();
  if (hl.empty()) {
    // Empty line -- means end of everything!
    state = kMessageDone;
    return 0;
  }
  if ((hl[0] == ' ') || (hl[0] == '\t')) {
    // "obs-fold" from RFC7230 section 3.2.4. Just ignore this line.
    return 0;
  }

  std::smatch matches;
  const std::string hls(hl);
  if (!std::regex_match(hls, matches, headerLineRegex)) {
    cerr << "Invalid trailer line: \"" << hl << '\"' << endl;
    return -2;
  }
  // We don't process trailer lines right now.
  return 0;
}

int HttpMessage::fillChunk(LineState* buf) {
  assert(chunked == 1);
  for (;;) {
    int s;
    switch (chunkState_) {
      case kChunkInit:
        s = parseChunkHeader(buf);
        break;
      case kChunkLength:
        s = parseChunkBody(buf);
        break;
      case kChunkChunk:
        s = parseChunkEnd(buf);
        break;
      case kChunkEnd:
        state = kMessageBody;
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

int HttpMessage::fill(LineState* buf) {
  for (;;) {
    int s;
    switch (state) {
      case kMessageInit:
        switch (type) {
          case Request:
            s = parseRequestLine(buf);
            break;
          case Response:
            s = parseStatus(buf);
            break;
          default:
            assert(0);
        }
        break;
      case kMessageStatus:
        s = parseHeaderLine(buf);
        break;
      case kMessageHeaders:
        if (!chunked) {
          s = parseLengthBody(buf);
        } else {
          s = fillChunk(buf);
        }
        break;
      case kMessageBody:
        s = parseTrailerLine(buf);
        break;
      case kMessageDone:
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

}  // namespace apib
