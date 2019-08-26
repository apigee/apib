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

#include <memory>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>

#include "src/apib_rand.h"
#include "src/apib_url.h"

#include "gtest/gtest.h"

TEST(URL, ParseGood1) {
  ASSERT_EQ(0, url_InitOne("http://foo.com:1234/bar?baz=yes"));
  const URLInfo* u1 = url_GetNext(nullptr);
  EXPECT_EQ(0, u1->isSsl);
  EXPECT_EQ(1234, u1->port);
  EXPECT_STREQ("/bar?baz=yes", u1->path);
  EXPECT_STREQ("foo.com:1234", u1->hostHeader);
  struct sockaddr_in* a = (struct sockaddr_in*)url_GetAddress(u1, 0, NULL);
  EXPECT_EQ(1234, ntohs(a->sin_port));
  url_Reset();

  ASSERT_EQ(0, url_InitOne("http://foo.com/bar?baz=yes"));
  const URLInfo* u1a = url_GetNext(nullptr);
  EXPECT_EQ(0, u1a->isSsl);
  EXPECT_EQ(80, u1a->port);
  EXPECT_STREQ("foo.com", u1a->hostHeader);
  EXPECT_STREQ("/bar?baz=yes", u1a->path);
  url_Reset();

  ASSERT_EQ(0, url_InitOne("http://foo.com:80/bar?baz=yes"));
  const URLInfo* u1a1 = url_GetNext(nullptr);
  EXPECT_EQ(0, u1a1->isSsl);
  EXPECT_EQ(80, u1a1->port);
  EXPECT_STREQ("foo.com", u1a1->hostHeader);
  EXPECT_STREQ("/bar?baz=yes", u1a1->path);
  url_Reset();

  ASSERT_EQ(0, url_InitOne("http://foo.com/"));
  const URLInfo* u1b = url_GetNext(nullptr);
  EXPECT_EQ(0, u1b->isSsl);
  EXPECT_EQ(80, u1b->port);
  EXPECT_STREQ("/", u1b->path);
  url_Reset();

  ASSERT_EQ(0, url_InitOne("http://foo.com:1000"));
  const URLInfo* u1c = url_GetNext(nullptr);
  EXPECT_EQ(0, u1c->isSsl);
  EXPECT_EQ(1000, u1c->port);
  EXPECT_STREQ("/", u1c->path);
  url_Reset();

  ASSERT_EQ(0, url_InitOne("http://foo.com/bar?baz=yes"));
  const URLInfo* u1d = url_GetNext(nullptr);
  EXPECT_EQ(0, u1d->isSsl);
  EXPECT_EQ(80, u1d->port);
  EXPECT_STREQ("/bar?baz=yes", u1d->path);
  url_Reset();

  ASSERT_EQ(0, url_InitOne("https://foo.com:1234/bar/baz"));
  const URLInfo* u2 = url_GetNext(nullptr);
  EXPECT_EQ(1, u2->isSsl);
  EXPECT_EQ(1234, u2->port);
  EXPECT_STREQ("/bar/baz", u2->path);
  url_Reset();

  ASSERT_EQ(0, url_InitOne("https://foo.com/bar?baz=yes"));
  const URLInfo* u2a = url_GetNext(nullptr);
  EXPECT_EQ(1, u2a->isSsl);
  EXPECT_EQ(443, u2a->port);
  EXPECT_STREQ("foo.com", u2a->hostHeader);
  EXPECT_STREQ("/bar?baz=yes", u2a->path);
  url_Reset();

  ASSERT_EQ(0, url_InitOne("https://foo.com:443/bar?baz=yes"));
  const URLInfo* u2b = url_GetNext(nullptr);
  EXPECT_EQ(1, u2b->isSsl);
  EXPECT_EQ(443, u2b->port);
  EXPECT_STREQ("foo.com", u2b->hostHeader);
  EXPECT_STREQ("/bar?baz=yes", u2b->path);
  url_Reset();
}

TEST(URL, ParseLocalhost) {
  // This test assumes that "localhost" always works
  ASSERT_EQ(0, url_InitOne("http://localhost"));
  const URLInfo* u = url_GetNext(nullptr);
  EXPECT_EQ(0, u->isSsl);
  EXPECT_EQ(80, u->port);
  EXPECT_STREQ("/", u->path);
  EXPECT_LE(1, u->addressCount);
  url_Reset();
}

TEST(URL, ParseFile) {
  auto rand = apib_InitRand();
  EXPECT_EQ(0, url_InitFile("test/data/urls.txt"));
  for (int i = 0; i < 10000; i++) {
    const URLInfo* u = url_GetNext(rand);
    ASSERT_NE(nullptr, u);
  }
  apib_FreeRand(rand);
}