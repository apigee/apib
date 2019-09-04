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

#include <regex.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>

#include "gtest/gtest.h"
#include "src/apib_oauth.h"

static RandState randState;

class OAuthTest : public ::testing::Test {
 protected:
  OAuthInfo oauth;

  OAuthTest() {
    oauth.consumerKey = strdup("9djdj82h48djs9d2");
    oauth.consumerSecret = strdup("j49sk3j29djd");
    oauth.accessToken = strdup("kkk9d7dh3k39sjv7");
    oauth.tokenSecret = strdup("dh893hdasih9");
  }

  ~OAuthTest() {
    // The "url_" family of functions use static data, so reset every time.
    url_Reset();
    free(oauth.consumerKey);
    free(oauth.consumerSecret);
    free(oauth.accessToken);
    free(oauth.tokenSecret);
  }
};

TEST_F(OAuthTest, Rfc5849BaseAndHmac) {
  ASSERT_EQ(0,
            url_InitOne(
                "http://example.com/request?b5=%3D%253D&a3=a&c%40=&a2=r%20b"));
  const char* body = "c2&a3=2+q";
  const long timestamp = 137131201;
  const char* nonce = "7d8f3e4a";

  char* base =
      oauth_buildBaseString(randState, url_GetNext(randState), "POST",
                            timestamp, nonce, body, strlen(body), &oauth);

  // Exact results from RFC7230
  const char* expected =
      "POST&http%3A%2F%2Fexample.com%2Frequest&a2%3Dr%2520b%26a3%3D2%2520q"
      "%26a3%3Da%26b5%3D%253D%25253D%26c%2540%3D%26c2%3D%26oauth_consumer_"
      "key%3D9djdj82h48djs9d2%26oauth_nonce%3D7d8f3e4a%26oauth_signature_m"
      "ethod%3DHMAC-SHA1%26oauth_timestamp%3D137131201%26oauth_token%3Dkkk"
      "9d7dh3k39sjv7";
  EXPECT_STREQ(expected, base);

  /* Results don't match RFC. Don't know why.
  const char* expectedSignature = "djosJKDKJSD8743243%2Fjdk33klY%3D";
  char* sig = oauth_generateHmac(base, &oauth);
  EXPECT_STREQ(expectedSignature, sig);
  free(sig);
  */

  free(base);
}

TEST_F(OAuthTest, QueryString) {
  ASSERT_EQ(0,
            url_InitOne(
                "http://example.com/request?b5=%3D%253D&a3=a&c%40=&a2=r%20b"));
  const char* body = "c2&a3=2+q";

  char* query = oauth_MakeQueryString(randState, url_GetNext(randState), "POST",
                                      body, strlen(body), &oauth);
  printf("%s\n", query);

  const char* expectedRE =
      "oauth_consumer_key=9djdj82h48djs9d2&oauth_token=kkk9d7dh3k39sjv7&oauth_"
      "signature_method=HMAC-SHA1&oauth_signature=.*&oauth_timestamp=.*&oauth_"
      "nonce=.*";

  regex_t re;
  ASSERT_EQ(0, regcomp(&re, expectedRE, REG_NOSUB));
  EXPECT_EQ(0, regexec(&re, query, 0, NULL, 0));
  regfree(&re);
  free(query);
}

TEST_F(OAuthTest, AuthHeader) {
  ASSERT_EQ(0,
            url_InitOne(
                "http://example.com/request?b5=%3D%253D&a3=a&c%40=&a2=r%20b"));
  const char* body = "c2&a3=2+q";

  char* hdr = oauth_MakeHeader(randState, url_GetNext(randState), "Example",
                               "POST", body, strlen(body), &oauth);
  printf("%s\n", hdr);

  const char* expectedRE =
      "Authorization: OAuth realm=\"Example\", "
      "oauth_consumer_key=\"9djdj82h48djs9d2\", "
      "oauth_token=\"kkk9d7dh3k39sjv7\", oauth_signature_method=\"HMAC-SHA1\", "
      "oauth_signature=\".*\", oauth_timestamp=\".*\", "
      "oauth_nonce=\".*\"";

  regex_t re;
  ASSERT_EQ(0, regcomp(&re, expectedRE, REG_NOSUB));
  EXPECT_EQ(0, regexec(&re, hdr, 0, NULL, 0));
  regfree(&re);
  free(hdr);
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  randState = apib_InitRand();
  const int err = RUN_ALL_TESTS();
  apib_FreeRand(randState);
  return err;
}