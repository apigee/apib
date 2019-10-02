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

#include <fstream>

#include "gtest/gtest.h"
#include "src/apib_lines.h"
#include "src/apib_message.h"

using apib::HttpMessage;
using apib::LineState;
using apib::Request;
using apib::Response;

namespace {

TEST(Message, GoodStatus) {
  LineState l("HTTP/1.1 200 Awesome!\r\n");
  l.setHttpMode(true);

  HttpMessage r(Response);
  ASSERT_EQ(0, r.fill(&l));
  EXPECT_EQ(apib::kMessageStatus, r.state);
  EXPECT_EQ(200, r.statusCode);
  EXPECT_EQ(1, r.majorVersion);
  EXPECT_EQ(1, r.minorVersion);
}

TEST(Message, GoodHeaders) {
  LineState l(
      "HTTP/1.1 201 CONTINUE\r\nFoo: bar\r\n extended "
      "stuff\r\nContent-Type:xxx\r\nContent-Length: 100  \r\n\r\n");
  l.setHttpMode(true);

  HttpMessage r(Response);
  ASSERT_EQ(0, r.fill(&l));
  EXPECT_EQ(apib::kMessageHeaders, r.state);
  EXPECT_EQ(201, r.statusCode);
  EXPECT_EQ(1, r.majorVersion);
  EXPECT_EQ(1, r.minorVersion);
  EXPECT_EQ(100, r.contentLength);
  EXPECT_EQ(0, r.chunked);
  EXPECT_EQ(false, r.shouldClose());
}

TEST(Message, CompleteResponse) {
  std::ifstream rf("./test/data/response.txt");
  ASSERT_NE(true, rf.fail());
  LineState l(100);
  l.setHttpMode(true);
  HttpMessage r(Response);

  do {
    l.readStream(rf);
    ASSERT_EQ(0, r.fill(&l));
    ASSERT_TRUE(l.consume());
  } while (r.state != apib::kMessageDone);

  EXPECT_EQ(200, r.statusCode);
  EXPECT_EQ(r.contentLength, r.bodyLength);
}

TEST(Message, LargerResponse) {
  std::ifstream rf("./test/data/largeresponse.txt");
  ASSERT_NE(true, rf.fail());
  LineState l(100);
  l.setHttpMode(true);
  HttpMessage r(Response);

  do {
    l.readStream(rf);
    ASSERT_EQ(0, r.fill(&l));
    ASSERT_TRUE(l.consume());
  } while (r.state != apib::kMessageDone);

  EXPECT_EQ(200, r.statusCode);
  EXPECT_EQ(r.contentLength, r.bodyLength);
}

TEST(Message, ChunkedResponse) {
  std::ifstream rf("./test/data/chunkedresponse.txt");
  ASSERT_NE(true, rf.fail());
  LineState l(100);
  l.setHttpMode(true);
  HttpMessage r(Response);

  do {
    int rc = l.readStream(rf);
    // Expect us to be done reading before the file is empty
    ASSERT_GT(rc, 0);
    ASSERT_EQ(0, r.fill(&l));
    ASSERT_TRUE(l.consume());
  } while (r.state != apib::kMessageDone);

  EXPECT_EQ(200, r.statusCode);
  EXPECT_EQ(103, r.bodyLength);
}

TEST(Message, LengthRequest) {
  std::ifstream rf("./test/data/bodyrequest.txt");
  ASSERT_NE(true, rf.fail());
  LineState l(100);
  l.setHttpMode(true);
  HttpMessage r(Request);

  do {
    l.readStream(rf);
    ASSERT_EQ(0, r.fill(&l));
    ASSERT_TRUE(l.consume());
  } while (r.state != apib::kMessageDone);

  EXPECT_EQ("GET", r.method);
  EXPECT_EQ("/foo?bar=baz&true=123", r.path);
  EXPECT_EQ(r.contentLength, r.bodyLength);
}

TEST(Message, GetRequest) {
  std::ifstream rf("./test/data/getrequest.txt");
  ASSERT_NE(true, rf.fail());
  LineState l(100);
  l.setHttpMode(true);
  HttpMessage r(Request);

  do {
    l.readStream(rf);
    ASSERT_EQ(0, r.fill(&l));
    ASSERT_TRUE(l.consume());
  } while (r.state != apib::kMessageDone);
  EXPECT_EQ("GET", r.method);
  EXPECT_EQ("/foo?bar=baz&true=123", r.path);
  EXPECT_EQ(0, r.contentLength);
}

}  // namespace