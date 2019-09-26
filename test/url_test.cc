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

#include <iostream>
#include <memory>
#include <netinet/in.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>

#include "src/apib_rand.h"
#include "src/apib_url.h"

#include "gtest/gtest.h"

using apib::URLInfo;

namespace {

TEST(URL, ParseGood1) {
  ASSERT_EQ(0, URLInfo::InitOne("http://notfound.notfound:1234/bar?baz=yes"));
  const URLInfo* u1 = URLInfo::GetNext(nullptr);
  EXPECT_EQ(false, u1->isSsl());
  EXPECT_EQ(1234, u1->port());
  EXPECT_EQ("/bar?baz=yes", u1->path());
  EXPECT_EQ("notfound.notfound:1234", u1->hostHeader());
  EXPECT_EQ("notfound.notfound", u1->hostName());
  EXPECT_EQ("/bar", u1->pathOnly());
  EXPECT_EQ("baz=yes", u1->query());
  struct sockaddr_in* a = (struct sockaddr_in*)u1->address(0, NULL);
  // Not a real address
  EXPECT_EQ(nullptr, a);
  URLInfo::Reset();

  ASSERT_EQ(0, URLInfo::InitOne("http://notfound.notfound/bar?baz=yes"));
  const URLInfo* u1a = URLInfo::GetNext(nullptr);
  EXPECT_EQ(0, u1a->isSsl());
  EXPECT_EQ(80, u1a->port());
  EXPECT_EQ("notfound.notfound", u1a->hostHeader());
  EXPECT_EQ("/bar?baz=yes", u1a->path());
  URLInfo::Reset();

  ASSERT_EQ(0, URLInfo::InitOne("http://notfound.notfound:80/bar?baz=yes"));
  const URLInfo* u1a1 = URLInfo::GetNext(nullptr);
  EXPECT_EQ(0, u1a1->isSsl());
  EXPECT_EQ(80, u1a1->port());
  EXPECT_EQ("notfound.notfound", u1a1->hostHeader());
  EXPECT_EQ("/bar?baz=yes", u1a1->path());
  URLInfo::Reset();

  ASSERT_EQ(0, URLInfo::InitOne("http://notfound.notfound/"));
  const URLInfo* u1b = URLInfo::GetNext(nullptr);
  EXPECT_EQ(0, u1b->isSsl());
  EXPECT_EQ(80, u1b->port());
  EXPECT_EQ("/", u1b->path());
  EXPECT_EQ("/", u1b->pathOnly());
  EXPECT_TRUE(u1b->query().empty());
  URLInfo::Reset();

  ASSERT_EQ(0, URLInfo::InitOne("http://notfound.notfound:1000"));
  const URLInfo* u1c = URLInfo::GetNext(nullptr);
  EXPECT_EQ(0, u1c->isSsl());
  EXPECT_EQ(1000, u1c->port());
  EXPECT_EQ("notfound.notfound", u1c->hostName());
  EXPECT_EQ("/", u1c->path());
  URLInfo::Reset();

  ASSERT_EQ(0, URLInfo::InitOne("http://notfound.notfound/bar?baz=yes"));
  const URLInfo* u1d = URLInfo::GetNext(nullptr);
  EXPECT_EQ(0, u1d->isSsl());
  EXPECT_EQ(80, u1d->port());
  EXPECT_EQ("/bar?baz=yes", u1d->path());
  URLInfo::Reset();

  ASSERT_EQ(0, URLInfo::InitOne("https://notfound.notfound:1234/bar/baz"));
  const URLInfo* u2 = URLInfo::GetNext(nullptr);
  EXPECT_EQ(1, u2->isSsl());
  EXPECT_EQ(1234, u2->port());
  EXPECT_EQ("/bar/baz", u2->path());
  EXPECT_EQ("/bar/baz", u2->pathOnly());
  EXPECT_TRUE(u2->query().empty());
  URLInfo::Reset();

  ASSERT_EQ(0, URLInfo::InitOne("https://notfound.notfound/bar?baz=yes"));
  const URLInfo* u2a = URLInfo::GetNext(nullptr);
  EXPECT_EQ(1, u2a->isSsl());
  EXPECT_EQ(443, u2a->port());
  EXPECT_EQ("notfound.notfound", u2a->hostHeader());
  EXPECT_EQ("/bar?baz=yes", u2a->path());
  URLInfo::Reset();

  ASSERT_EQ(0, URLInfo::InitOne("https://notfound.notfound:443/bar?baz=yes"));
  const URLInfo* u2b = URLInfo::GetNext(nullptr);
  EXPECT_EQ(1, u2b->isSsl());
  EXPECT_EQ(443, u2b->port());
  EXPECT_EQ("notfound.notfound", u2b->hostHeader());
  EXPECT_EQ("notfound.notfound", u2b->hostName());
  EXPECT_EQ("/bar?baz=yes", u2b->path());
  URLInfo::Reset();

  // From RFC5849
  ASSERT_EQ(0, URLInfo::InitOne("http://example.com/r%20v/X?id=123"));
  const URLInfo* r1 = URLInfo::GetNext(nullptr);
  EXPECT_EQ(0, r1->isSsl());
  EXPECT_EQ(80, r1->port());
  EXPECT_EQ("example.com", r1->hostHeader());
  EXPECT_EQ("example.com", r1->hostName());
  EXPECT_EQ("/r%20v/X?id=123", r1->path());
  EXPECT_EQ("/r%20v/X", r1->pathOnly());
  EXPECT_EQ("id=123", r1->query());
  URLInfo::Reset();

  ASSERT_EQ(0, URLInfo::InitOne("http://example.net:8080/?q=1"));
  const URLInfo* r2 = URLInfo::GetNext(nullptr);
  EXPECT_EQ(false, r2->isSsl());
  EXPECT_EQ(8080, r2->port());
  EXPECT_EQ("example.net:8080", r2->hostHeader());
  EXPECT_EQ("example.net", r2->hostName());
  EXPECT_EQ("/?q=1", r2->path());
  EXPECT_EQ("/", r2->pathOnly());
  EXPECT_EQ("q=1", r2->query());
  URLInfo::Reset();
}

TEST(URL, ParseLocalhost) {
  // This test assumes that "localhost" always works
  ASSERT_EQ(0, URLInfo::InitOne("http://localhost"));
  const URLInfo* u = URLInfo::GetNext(nullptr);
  ASSERT_NE(nullptr, u);
  EXPECT_EQ(0, u->isSsl());
  EXPECT_EQ(80, u->port());
  EXPECT_EQ("/", u->path());
  EXPECT_LE(1, u->addressCount());
  URLInfo::Reset();
}

TEST(URL, ParseFile) {
  auto rand = apib_InitRand();
  EXPECT_EQ(0, URLInfo::InitFile("test/data/urls.txt"));
  for (int i = 0; i < 10000; i++) {
    const URLInfo* u = URLInfo::GetNext(rand);
    ASSERT_NE(nullptr, u);
  }
  apib_FreeRand(rand);
}

} // namespace
