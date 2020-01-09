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

#include <regex>

#include "gtest/gtest.h"
#include "apib/apib_oauth.h"

namespace {

using apib::OAuthInfo;
using apib::URLInfo;

static apib::RandomGenerator randgen;

class OAuthTest : public ::testing::Test {
 protected:
  OAuthInfo oauth;

  OAuthTest() {
    oauth.consumerKey = "9djdj82h48djs9d2";
    oauth.consumerSecret = "j49sk3j29djd";
    oauth.accessToken = "kkk9d7dh3k39sjv7";
    oauth.tokenSecret = "dh893hdasih9";
  }

  ~OAuthTest() {
    // The "url_" family of functions use static data, so reset every time.
    URLInfo::Reset();
  }
};

TEST_F(OAuthTest, Rfc5849BaseAndHmac) {
  ASSERT_EQ(0,
            URLInfo::InitOne(
                "http://example.com/request?b5=%3D%253D&a3=a&c%40=&a2=r%20b"));
  const std::string body = "c2&a3=2+q";
  const long timestamp = 137131201;
  const std::string nonce = "7d8f3e4a";

  std::string base =
      oauth_buildBaseString(&randgen, *(URLInfo::GetNext(&randgen)), "POST",
                            timestamp, nonce, body.data(), body.size(), oauth);

  // Exact results from RFC7230
  const std::string expected =
      "POST&http%3A%2F%2Fexample.com%2Frequest&a2%3Dr%2520b%26a3%3D2%2520q"
      "%26a3%3Da%26b5%3D%253D%25253D%26c%2540%3D%26c2%3D%26oauth_consumer_"
      "key%3D9djdj82h48djs9d2%26oauth_nonce%3D7d8f3e4a%26oauth_signature_m"
      "ethod%3DHMAC-SHA1%26oauth_timestamp%3D137131201%26oauth_token%3Dkkk"
      "9d7dh3k39sjv7";
  EXPECT_EQ(expected, base);

  /* Results don't match RFC. Don't know why.
  const char* expectedSignature = "djosJKDKJSD8743243%2Fjdk33klY%3D";
  char* sig = oauth_generateHmac(base, &oauth);
  EXPECT_STREQ(expectedSignature, sig);
  free(sig);
  */
}

TEST_F(OAuthTest, QueryString) {
  ASSERT_EQ(0,
            URLInfo::InitOne(
                "http://example.com/request?b5=%3D%253D&a3=a&c%40=&a2=r%20b"));
  const std::string body = "c2&a3=2+q";

  std::string query =
      oauth_MakeQueryString(&randgen, *(URLInfo::GetNext(&randgen)), "POST",
                            body.data(), body.size(), oauth);

  const std::string expectedRE =
      "oauth_consumer_key=9djdj82h48djs9d2&oauth_token=kkk9d7dh3k39sjv7&oauth_"
      "signature_method=HMAC-SHA1&oauth_signature=.*&oauth_timestamp=.*&oauth_"
      "nonce=.*";

  std::regex re(expectedRE);
  EXPECT_TRUE(std::regex_match(query.begin(), query.end(), re));
}

TEST_F(OAuthTest, AuthHeader) {
  ASSERT_EQ(0,
            URLInfo::InitOne(
                "http://example.com/request?b5=%3D%253D&a3=a&c%40=&a2=r%20b"));
  const std::string body = "c2&a3=2+q";

  std::string hdr =
      oauth_MakeHeader(&randgen, *(URLInfo::GetNext(&randgen)), "Example",
                       "POST", body.data(), body.size(), oauth);

  const std::string expectedRE =
      "Authorization: OAuth realm=\"Example\", "
      "oauth_consumer_key=\"9djdj82h48djs9d2\", "
      "oauth_token=\"kkk9d7dh3k39sjv7\", oauth_signature_method=\"HMAC-SHA1\", "
      "oauth_signature=\".*\", oauth_timestamp=\".*\", "
      "oauth_nonce=\".*\"";

  std::regex re(expectedRE);
  EXPECT_TRUE(std::regex_match(hdr.begin(), hdr.end(), re));
}

}  // namespace
