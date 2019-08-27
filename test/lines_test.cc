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

#include "src/apib_lines.h"

#include "gtest/gtest.h"

TEST(Lines, AllFull) {
  LineState l;

  const char* const DATA =
      "Line one\nLine two\nLine three\n";
  char* realData = strdup(DATA);
  auto realLen = strlen(DATA);

  linep_Start(&l, realData, realLen, realLen);
  ASSERT_NE(0, linep_NextLine(&l));
  ASSERT_STREQ("Line one", linep_GetLine(&l));
  ASSERT_NE(0, linep_NextLine(&l));
  ASSERT_STREQ("Line two", linep_GetLine(&l));
  ASSERT_NE(0, linep_NextLine(&l));
  ASSERT_STREQ("Line three", linep_GetLine(&l));
  ASSERT_EQ(0, linep_NextLine(&l));
  free(realData);
}

TEST(Lines, SlowFill) {
  LineState l;

  // Empty buffer, no line
  const int bufLen = 100;
  char* buf = (char*)malloc(bufLen);
  linep_Start(&l, buf, bufLen, 0);
  ASSERT_EQ(0, linep_NextLine(&l));
  ASSERT_EQ(nullptr, linep_GetLine(&l));
  ASSERT_EQ(0, linep_Reset(&l));

  // Add a line and a half
  const char* const CHUNK1 = "Line one\nLin";
  char* chunk = strdup(CHUNK1);
  auto chunkLen = strlen(CHUNK1);

  char* writePos;
  int spaceLeft;
  linep_GetReadInfo(&l, &writePos, &spaceLeft);
  ASSERT_LE(chunkLen, spaceLeft);
  memcpy(writePos, chunk, chunkLen);
  linep_SetReadLength(&l, chunkLen);
  free(chunk);

  // Now we can read the first line
  ASSERT_NE(0, linep_NextLine(&l));
  ASSERT_STREQ("Line one", linep_GetLine(&l));
  ASSERT_EQ(0, linep_NextLine(&l));

  // And now we can add the rest
  const char* const CHUNK2 = "e two\r\n\r\nLast line\n";
  chunk = strdup(CHUNK2);
  chunkLen = strlen(CHUNK2);
  ASSERT_EQ(0, linep_Reset(&l));
  linep_GetReadInfo(&l, &writePos, &spaceLeft);
  ASSERT_LE(chunkLen, spaceLeft);
  memcpy(writePos, chunk, chunkLen);
  linep_SetReadLength(&l, chunkLen);
  free(chunk);

  // Now we should have two more lines
  ASSERT_NE(0, linep_NextLine(&l));
  ASSERT_STREQ("Line two", linep_GetLine(&l));
  ASSERT_NE(0, linep_NextLine(&l));
  ASSERT_STREQ("Last line", linep_GetLine(&l));
  ASSERT_EQ(0, linep_NextLine(&l));

  free(buf);
}

TEST(Lines, Tokens) {
  LineState l;

  // Empty buffer, no line
  const int bufLen = 100;
  char* buf = (char*)malloc(bufLen);
  linep_Start(&l, buf, bufLen, 0);
  ASSERT_EQ(0, linep_NextLine(&l));
  ASSERT_EQ(nullptr, linep_GetLine(&l));
  ASSERT_EQ(0, linep_Reset(&l));

  // Add half a line
  const char* const CHUNK1 = "Newval";
  char* chunk = strdup(CHUNK1);
  auto chunkLen = strlen(CHUNK1);

  char* writePos;
  int spaceLeft;
  linep_GetReadInfo(&l, &writePos, &spaceLeft);
  ASSERT_LE(chunkLen, spaceLeft);
  memcpy(writePos, chunk, chunkLen);
  linep_SetReadLength(&l, chunkLen);
  free(chunk);

  // No line. Now we need to add the rest
  ASSERT_EQ(0, linep_NextLine(&l));
  ASSERT_EQ(0, linep_Reset(&l));

  const char* const CHUNK2 = "ue: Foobar\n";
  chunk = strdup(CHUNK2);
  chunkLen = strlen(CHUNK2);
  linep_GetReadInfo(&l, &writePos, &spaceLeft);
  ASSERT_LE(chunkLen, spaceLeft);
  memcpy(writePos, chunk, chunkLen);
  linep_SetReadLength(&l, chunkLen);
  free(chunk);

  // Now we have a line with a token in it
  ASSERT_NE(0, linep_NextLine(&l));
  ASSERT_STREQ("Newvalue", linep_NextToken(&l, ": "));
  ASSERT_STREQ("Foobar", linep_NextToken(&l, ": "));
  ASSERT_EQ(nullptr, linep_NextToken(&l, ": "));

  free(buf);
}

TEST(Lines, HttpMode) {
  LineState l;

  char* buf = strdup("One\r\nTwo\r\n\r\nThree\r\n\r\n");
  size_t len = strlen(buf);
  linep_Start(&l, buf, len + 1, len);
  linep_SetHttpMode(&l, 1);

  ASSERT_NE(0, linep_NextLine(&l));
  EXPECT_STREQ("One", linep_GetLine(&l));
  ASSERT_NE(0, linep_NextLine(&l));
  EXPECT_STREQ("Two", linep_GetLine(&l));
  ASSERT_NE(0, linep_NextLine(&l));
  EXPECT_STREQ("", linep_GetLine(&l));
  ASSERT_NE(0, linep_NextLine(&l));
  EXPECT_STREQ("Three", linep_GetLine(&l));
  ASSERT_NE(0, linep_NextLine(&l));
  EXPECT_STREQ("", linep_GetLine(&l));
  ASSERT_EQ(0, linep_NextLine(&l));

  free(buf);
}

TEST(Lines, TooLong) {
  LineState l;

  // Empty buffer, no line
  const int bufLen = 20;
  char* buf = (char*)malloc(bufLen);
  linep_Start(&l, buf, bufLen, 0);
  ASSERT_EQ(0, linep_NextLine(&l));
  ASSERT_EQ(nullptr, linep_GetLine(&l));
  ASSERT_EQ(0, linep_Reset(&l));

  // Add half a line
  const char* const CHUNK1 = "0123456789";
  char* chunk = strdup(CHUNK1);
  auto chunkLen = strlen(CHUNK1);

  char* writePos;
  int spaceLeft;
  linep_GetReadInfo(&l, &writePos, &spaceLeft);
  ASSERT_LE(chunkLen, spaceLeft);
  memcpy(writePos, chunk, chunkLen);
  linep_SetReadLength(&l, chunkLen);
  free(chunk);

  // No line. Now we need to add the rest
  ASSERT_EQ(0, linep_NextLine(&l));
  ASSERT_EQ(0, linep_Reset(&l));

  const char* const CHUNK2 = "0123456789";
  chunk = strdup(CHUNK2);
  chunkLen = strlen(CHUNK2);
  linep_GetReadInfo(&l, &writePos, &spaceLeft);
  ASSERT_LE(chunkLen, spaceLeft);
  memcpy(writePos, chunk, chunkLen);
  linep_SetReadLength(&l, chunkLen);
  free(chunk);

  // We still don't have a line, and we can't add one
  ASSERT_EQ(0, linep_NextLine(&l));
  ASSERT_NE(0, linep_Reset(&l));

  free(buf);
}

TEST(Lines, StringBufEmpty) {
  StringBuf b;
  buf_New(&b, 10);
  EXPECT_EQ(0, buf_Length(&b));
  EXPECT_STREQ("", buf_Get(&b));
  buf_Free(&b);
}

TEST(Lines, StringBufAppend) {
  StringBuf b;
  buf_New(&b, 10);
  buf_Append(&b, "One");
  EXPECT_STREQ("One", buf_Get(&b));
  // Something more than 10 bytes long
  buf_Append(&b, "TwoTwoTwoTwoTwo");
  EXPECT_STREQ("OneTwoTwoTwoTwoTwo", buf_Get(&b));
  // Something more than 10 bytes long
  buf_Append(&b, "01234567890123456789012345");
  EXPECT_STREQ("OneTwoTwoTwoTwoTwo01234567890123456789012345", buf_Get(&b));
  buf_AppendChar(&b, 'x');
  EXPECT_STREQ("OneTwoTwoTwoTwoTwo01234567890123456789012345x", buf_Get(&b));
  buf_Free(&b);
}

TEST(Lines, StringBufAppendBoundary) {
  StringBuf b;
  buf_New(&b, 10);
  buf_Append(&b, "0123456789");
  EXPECT_STREQ("0123456789", buf_Get(&b));
  // Something more than 10 bytes long
  buf_Append(&b, "01234567890123456789");
  EXPECT_STREQ("012345678901234567890123456789", buf_Get(&b));
  // Something more than 10 bytes long
  buf_Append(&b, "Foo");
  EXPECT_STREQ("012345678901234567890123456789Foo", buf_Get(&b));
  buf_Free(&b);
}

TEST(Lines, StringBufPrintf) {
  StringBuf b;
  buf_New(&b, 10);
  buf_Printf(&b, "Foo %s ", "the");
  EXPECT_STREQ("Foo the ", buf_Get(&b));
  // Something more than 10 bytes long
  buf_Printf(&b, "Bar has %i drinks, including %s", 99, "bourbon");
  EXPECT_STREQ("Foo the Bar has 99 drinks, including bourbon", buf_Get(&b));
  buf_Free(&b);
}