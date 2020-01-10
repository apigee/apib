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

#ifndef APIB_STATUS_H
#define APIB_STATUS_H

#include <ostream>
#include <string>

#include "absl/strings/string_view.h"

namespace apib {

class Status {
 public:
  enum Code {
    OK,
    SOCKET_ERROR,
    TLS_ERROR,
    DNS_ERROR,
    INVALID_URL,
    IO_ERROR,
    INTERNAL_ERROR
  };

  static const Status& kOk;

  // Initialize an OK status
  Status() : code_(OK) {}
  // Initialize a generic status with the specified code
  Status(Code c) : code_(c) {}
  // Include a custom message with the code
  Status(Code c, absl::string_view msg) : code_(c), msg_(msg) {}
  // Set the message to the value of "strerror" for "errnum"
  Status(Code c, int errnum);

  Code code() const { return code_; }
  std::string message() const { return msg_; }
  bool ok() const { return code_ == OK; }

  std::string codeString() const;
  std::string str() const;
  friend std::ostream& operator<<(std::ostream& out, const Status& s);

 private:
  Code code_;
  std::string msg_;
};

template <class T>
class StatusOr {
 public:
  StatusOr() {}
  StatusOr(Status s) : status_(s) {}
  StatusOr(const T& v) : status_(), value_(v) {}
  StatusOr(T&& v) : status_(), value_(std::move(v)) {}

  Status status() const { return status_; }
  T value() const { return value_; }
  void movevalue(T&& target) { target = std::move(value_); }
  const T& valueref() const { return value_; }
  T* valueptr() { return &value_; }
  bool ok() const { return status_.ok(); }

  std::string str() const { return status_.str(); }
  friend std::ostream& operator<<(std::ostream& out, const StatusOr<T>& s) {
    out << s.status_;
    return out;
  }

 private:
  Status status_;
  T value_;
};

}  // namespace apib

#endif