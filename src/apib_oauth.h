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

#include <string>

#include "src/apib_rand.h"
#include "src/apib_url.h"

namespace apib {

class OAuthInfo {
 public:
  std::string consumerKey;
  std::string consumerSecret;
  std::string accessToken;
  std::string tokenSecret;
};

// Generate an OAuth 1.0a query string for the specified URL,
// form body content, and security tokens. The result must be freed
// using free().
extern std::string oauth_MakeQueryString(
    RandomGenerator* rand, const URLInfo& url, const std::string& method,
    const char* sendData, unsigned int sendDataSize, const OAuthInfo& oauth);

// Do the same but put the result into an HTTP "Authorization:" header.
extern std::string oauth_MakeHeader(RandomGenerator* rand, const URLInfo& url,
                                    const std::string& realm,
                                    const std::string& method,
                                    const char* sendData,
                                    unsigned int sendDataSize,
                                    const OAuthInfo& oauth);

// Externalized for testing
extern std::string oauth_buildBaseString(
    RandomGenerator* rand, const URLInfo& url, const std::string& method,
    long timestamp, const std::string& nonce, const char* sendData,
    size_t sendDataSize, const OAuthInfo& oauth);
extern std::string oauth_generateHmac(const std::string& base,
                                      const OAuthInfo& oauth);

}  // namespace apib

#endif  // APIB_OAUTH_H
