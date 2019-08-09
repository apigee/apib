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
#include "src/apib_lines.h"

#include "gtest/gtest.h"

TEST(Response, GoodStatus) {
  message_Init();
  char* buf = strdup("HTTP/1.1 200 Awesome!\r\n");
  size_t len = strlen(buf);
  LineState l;
  linep_Start(&l, buf, len + 1, len);

  HttpMessage* r = message_NewResponse();
  ASSERT_EQ(0, message_Fill(r, &l));
  EXPECT_EQ(MESSAGE_STATUS, r->state);
  EXPECT_EQ(200, r->statusCode);
  EXPECT_EQ(1, r->majorVersion);
  EXPECT_EQ(1, r->minorVersion);

  free(buf);
}

TEST(Response, GoodHeaders) {
  message_Init();
  char* buf = strdup("HTTP/1.1 201 CONTINUE\r\nFoo: bar\r\n extended stuff\r\nContent-Type:xxx\r\nContent-Length: 100  \r\n\r\n");
  size_t len = strlen(buf);
  LineState l;
  linep_Start(&l, buf, len + 1, len);
  linep_SetHttpMode(&l, 1);

  HttpMessage* r = message_NewResponse();
  ASSERT_EQ(0, message_Fill(r, &l));
  EXPECT_EQ(MESSAGE_HEADERS, r->state);
  EXPECT_EQ(201, r->statusCode);
  EXPECT_EQ(1, r->majorVersion);
  EXPECT_EQ(1, r->minorVersion);
  EXPECT_EQ(100, r->contentLength);
  EXPECT_EQ(0, r->chunked);
  EXPECT_EQ(0, r->shouldClose);

  free(buf);
}

TEST(Response, CompleteResponse) {
  message_Init();
  FILE* rf = fopen("./test/data/response.txt", "r");
  ASSERT_NE(nullptr, rf);
  LineState l;
  char* buf = (char*)malloc(100);
  linep_Start(&l, buf, 100, 0);
  linep_SetHttpMode(&l, 1);

  HttpMessage* r = message_NewResponse();

  do {
    linep_ReadFile(&l, rf);
    ASSERT_EQ(0, message_Fill(r, &l));
    linep_Reset(&l);
  } while (r->state != MESSAGE_DONE);
  fclose(rf);

  EXPECT_EQ(200, r->statusCode);
  EXPECT_EQ(r->contentLength, r->bodyLength);

  message_Free(r);
  free(buf);
}

TEST(Response, ChunkedResponse) {
  message_Init();
  FILE* rf = fopen("./test/data/chunkedresponse.txt", "r");
  ASSERT_NE(nullptr, rf);
  LineState l;
  char* buf = (char*)malloc(100);
  linep_Start(&l, buf, 100, 0);
  linep_SetHttpMode(&l, 1);

  HttpMessage* r = message_NewResponse();

  do {
    int rc = linep_ReadFile(&l, rf);
    // Expect us to be done reading before the file is empty
    ASSERT_GT(rc, 0);
    ASSERT_EQ(0, message_Fill(r, &l));
    linep_Reset(&l);
  } while (r->state != MESSAGE_DONE);
  fclose(rf);

  EXPECT_EQ(200, r->statusCode);
  EXPECT_EQ(103, r->bodyLength);

  message_Free(r);
  free(buf);
}