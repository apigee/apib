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

#include <istream>
#include <ostream>
#include <string>

namespace apib {

/*
 * Code for managing line-oriented input, that must be broken into lines,
 * then tokenized, and which might come a little bit at a time.
 */

class LineState {
 public:
  /* Initialize or reset a LineState with new data. "size" is the size
   * of the buffer, and "len" is the amount that's currently filled with real stuff.
   * This object takes over "line". */
  LineState(char* line, int size, int len);
  /* Initialize with an empty buffer. */
  LineState(size_t len);
  ~LineState();
  /* Reset the structure but keep the original buffer the same.
   Does not reset HTTP mode. This lets you re-use without reallocating. */
  void clear();
  /* If set to true, then every line is terminated by a single CRLF. Otherwise
   we eat them all up and return no blank lines. Http relies on blank lines! */
  void setHttpMode(bool on);
  /* Read the first complete line -- return false if a complete line is not present. */
  bool next();
  /* If NextLine returned non-zero, return a pointer to the entire line */
  std::string line();
  /* If NextLine returned non-zero, return the next token delimited by "toks" like strtok */
  std::string nextToken(const std::string& toks);
  /* Move any data remaining in the line to the start. Used if we didn't
   read a complete line and are still expecting more data. 
   If we return false, it means that the buffer is full and we don't
   have a complete line. Implementations should know that means that the lines
   are too long and stop processing. */
  bool consume();
  /* Fill the line buffer with data from a file. Return what the read call did. */
  int readStream(std::istream& in);
  /* Fill the buffer with data from a socket */
  int readFd(int fd);
  /* Get info to fill the rest of the buffer directly. When done,
    buf points to where to write more data, and remaining tells how much space
    is left in the buffer. */
  void getReadInfo(char** buf, int* remaining) const;
  /* Find out how much data is left unprocessed */
  int dataRemaining() const { return (bufLen_ - lineEnd_); }
  /* Write data from the end of the last line to the end of the buffer */
  void writeRemaining(std::ostream& out) const;
  /* Skip forward to see if there's another line */
  void skip(int toSkip) { lineEnd_ += toSkip; }
  /* Report back how much we read */
  void setReadLength(int len) { bufLen_ += len; }
  /* Output the buffer to a stream */
  void debug(std::ostream& out) const;

 private:
  void nullLast();

  char*    buf_;
  bool     httpMode_; /* Lines are terminated by only a single CRLF. */
  int      bufLen_; /* The number of valid bytes in the buffer */
  int      bufSize_; /* Number of allocated bytes in case it is different */
  int      lineStart_;
  int      lineEnd_;
  bool     lineComplete_;
  int      tokStart_;
  int      tokEnd_;
};

}  // namespace apib

#endif  // APIB_LINEP_H
