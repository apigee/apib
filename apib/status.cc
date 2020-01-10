/*
Copyright 2020 Google LLC

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

#include "apib/status.h"

#include <cstring>
#include <sstream>

namespace apib {

static const Status kOkStatus(Status::Code::OK);
const Status& Status::kOk = kOkStatus;
static const int kMaxStatusLen = 64;

Status::Status(Code c, int errnum) : code_(c) {
  char msgbuf[kMaxStatusLen];
  strerror_r(errnum, msgbuf, kMaxStatusLen);
  msg_ = msgbuf;
}

std::string Status::codeString() const {
  switch (code_) {
    case OK:
      return "ok";
    case SOCKET_ERROR:
      return "Socket error";
    case TLS_ERROR:
      return "TLS error";
    case DNS_ERROR:
      return "DNS error";
    case INVALID_URL:
      return "Invalid URL";
    case IO_ERROR:
      return "I/O error";
    case INTERNAL_ERROR:
      return "Internal error";
    default:
      return "Unknown error";
  }
}

std::ostream& operator<<(std::ostream& out, const Status& s) {
  out << s.codeString();
  if (!s.msg_.empty()) {
    out << ": " << s.msg_;
  }
  return out;
}

std::string Status::str() const {
  std::ostringstream ss;
  ss << *this;
  return ss.str();
}

}  // namespace apib