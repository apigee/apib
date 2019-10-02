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

#ifndef APIB_MESSAGE_H
#define APIB_MESSAGE_H

#include <cstdint>
#include <string>

#include "absl/strings/string_view.h"
#include "src/apib_lines.h"

namespace apib {

typedef enum { Request, Response } MessageType;

// State values. They are increasing integers so that you can do a ">"
extern const int kMessageInit;
extern const int kMessageStatus;
extern const int kMessageHeaders;
extern const int kMessageBody;
extern const int kMessageDone;

extern const int kChunkInit;
extern const int kChunkLength;
extern const int kChunkChunk;
extern const int kChunkEnd;

class HttpMessage {
 public:
  HttpMessage(MessageType t);

  /*
  Add data to a response object. The data should consist of a valid
  HTTP response. Returns 0 on success and non-zero on error.
  Callers should check the "state" parameter and keep feeding data
  until the state is kMessageDone. An error means that we got
  invalid HTTP data. The passed "LineState" MUST be in "HTTP" mode.
  */
  int fill(LineState* buf);

  // Clear the state of the message, except for "type," so that
  // it may be reused without being reallocated.
  void clear();

  // If state > kMessageHeaders, tells us whether we should
  // close the connection when done.
  bool shouldClose() const { return (shouldClose_ > 0); }

  // Public things so that you can easily query them

  const MessageType type;
  int state;

  // Available when state >= kMessageStatus
  int majorVersion;
  int minorVersion;

  // Only available for a response
  int statusCode;

  // Only available for a request
  std::string method;
  std::string path;

  // Available when state >= kMessageHeaders
  //   The value of the "content-length" header, if set
  int32_t contentLength;
  int chunked;

  // Available when state >= kMessageBody
  //    The total length of the message body that we read,
  //    whether content-length was set or chunked encoding was used.
  int32_t bodyLength;

 private:
  int parseStatus(LineState* buf);
  int parseRequestLine(LineState* buf);
  void finishHeaders();
  int parseHeaderLine(LineState* buf);
  void examineHeader(const absl::string_view name,
                     const absl::string_view value);
  int parseLengthBody(LineState* buf);
  int parseChunkHeader(LineState* buf);
  int parseChunkBody(LineState* buf);
  int parseChunkEnd(LineState* buf);
  int parseTrailerLine(LineState* buf);
  int fillChunk(LineState* buf);

  int shouldClose_;
  int chunkState_;
  int32_t chunkLength_;
  int32_t chunkPosition_;
};

}  // namespace apib

#endif  // APIB_MESSAGE_H