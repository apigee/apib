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

#include "apib_lines.h"

#include <unistd.h>

#include <cstring>

namespace apib {

static int isChar(const char c, const char* match) {
  unsigned int m = 0;

  while (match[m] != 0) {
    if (c == match[m]) {
      return 1;
    }
    m++;
  }
  return 0;
}

LineState::LineState(char* line, int size, int len) {
  httpMode_ = false;
  buf_ = line;
  bufSize_ = size;
  bufLen_ = len;
  lineStart_ = lineEnd_ = 0;
  tokStart_ = tokEnd_ = 0;
  lineComplete_ = false;
}

LineState::LineState(const std::string& s) {
  httpMode_ = false;
  buf_ = (char*)malloc(s.size());
  bufSize_ = s.size();
  bufLen_ = s.size();
  lineStart_ = lineEnd_ = 0;
  tokStart_ = tokEnd_ = 0;
  lineComplete_ = false;
  memcpy(buf_, s.data(), bufSize_);
}

LineState::LineState(size_t len) {
  httpMode_ = false;
  buf_ = (char*)malloc(len);
  bufSize_ = len;
  bufLen_ = 0;
  lineStart_ = lineEnd_ = 0;
  tokStart_ = tokEnd_ = 0;
  lineComplete_ = false;
}

LineState::~LineState() { free(buf_); }

void LineState::clear() {
  bufLen_ = 0;
  lineStart_ = lineEnd_ = 0;
  tokStart_ = tokEnd_ = 0;
  lineComplete_ = false;
}

void LineState::nullLast() {
  buf_[lineEnd_] = 0;
  lineEnd_++;
}

bool LineState::next() {
  if (lineEnd_ > 0) {
    lineStart_ = lineEnd_;
  }
  if (lineEnd_ >= bufLen_) {
    lineComplete_ = false;
    return false;
  }

  /* Move to the first newline character */
  while ((lineEnd_ < bufLen_) && !isChar(buf_[lineEnd_], "\r\n")) {
    lineEnd_++;
  }
  if (lineEnd_ >= bufLen_) {
    /* Incomplete line in the buffer */
    lineComplete_ = false;
    return false;
  }

  if (httpMode_) {
    if (buf_[lineEnd_] == '\r') {
      nullLast();
      if (buf_[lineEnd_] == '\n') {
        nullLast();
      }
    } else {
      nullLast();
    }
  } else {
    /* Overwrite all newlines with nulls */
    while ((lineEnd_ < bufLen_) && isChar(buf_[lineEnd_], "\r\n")) {
      nullLast();
    }
  }

  tokStart_ = tokEnd_ = lineStart_;

  lineComplete_ = true;
  return true;
}

absl::string_view LineState::line() {
  if (!lineComplete_) {
    return "";
  }
  return absl::string_view(buf_ + lineStart_);
}

absl::string_view LineState::nextToken(const std::string& toks) {
  if (!lineComplete_) {
    return "";
  }
  if (tokEnd_ >= lineEnd_) {
    return "";
  }

  tokStart_ = tokEnd_;

  if (!toks.empty()) {
    while ((tokEnd_ < lineEnd_) && !isChar(buf_[tokEnd_], toks.c_str())) {
      tokEnd_++;
    }
    while ((tokEnd_ < lineEnd_) && isChar(buf_[tokEnd_], toks.c_str())) {
      buf_[tokEnd_] = 0;
      tokEnd_++;
    }
  }

  return absl::string_view(buf_ + tokStart_);
}

void LineState::skipMatches(const std::string& toks) {
  while ((tokEnd_ < lineEnd_) && isChar(buf_[tokEnd_], toks.c_str())) {
    tokEnd_++;
  }
}

void LineState::skip(int toSkip) {
  lineStart_ += toSkip;
  lineEnd_ += toSkip;
}

bool LineState::consume() {
  int remaining;
  if (!lineComplete_) {
    remaining = bufLen_ - lineStart_;
    memmove(buf_, buf_ + lineStart_, remaining);
  } else {
    remaining = 0;
  }
  bufLen_ = remaining;
  lineStart_ = lineEnd_ = 0;
  lineComplete_ = 0;
  return (remaining < bufSize_);
}

int LineState::readStream(std::istream& in) {
  const std::streamsize len = bufSize_ - bufLen_;
  in.read(buf_ + bufLen_, len);
  const std::streamsize r = in.gcount();
  bufLen_ += r;
  return r;
}

int LineState::readFd(int fd) {
  const int len = bufSize_ - bufLen_;
  const size_t r = read(fd, buf_ + bufLen_, len);
  if (r <= 0) {
    return r;
  }
  bufLen_ += r;
  return r;
}

void LineState::getReadInfo(char** buf, int* remaining) const {
  if (buf != nullptr) {
    *buf = buf_ + bufLen_;
  }
  if (remaining != nullptr) {
    *remaining = bufSize_ - bufLen_;
  }
}

void LineState::writeRemaining(std::ostream& out) const {
  const size_t len = bufLen_ - lineEnd_;
  out.write(buf_ + lineEnd_, len);
}

void LineState::debug(std::ostream& out) const {
  out << "buf len = " << bufLen_ << " line start = " << lineStart_
      << " end = " << lineEnd_ << " tok start = " << tokStart_
      << " tok end = " << tokEnd_ << std::endl;
  out.write(buf_, bufLen_);
  out << std::endl;
}

}  // namespace apib