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

#ifndef APIB_OAUTH_H
#define APIB_OAUTH_H

#include "src/apib_rand.h"
#include "src/apib_url.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  char* consumerKey;
  char* consumerSecret;
  char* accessToken;
  char* tokenSecret;
} OAuthInfo;

// Generate an OAuth 1.0a query string for the specified URL,
// form body content, and security tokens. The result must be freed
// using free().
extern char* oauth_MakeQueryString(RandState rand, const URLInfo* url,
                                   const char* method, const char* sendData,
                                   unsigned int sendDataSize,
                                   const OAuthInfo* oauth);

// Externalized for testing
extern char* oauth_buildBaseString(RandState rand, const URLInfo* url,
                                   const char* method, long timestamp, const char* nonce,
                                   const char* sendData,
                                   size_t sendDataSize, const OAuthInfo* oauth);
extern char* oauth_generateHmac(const char* base, const OAuthInfo* oauth);

#ifdef __cplusplus
}
#endif

#endif  // APIB_OAUTH_H
