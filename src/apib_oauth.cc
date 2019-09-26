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

#include "src/apib_oauth.h"

#include <assert.h>
#include <openssl/bio.h>
#include <openssl/hmac.h>
#include <openssl/sha.h>

#include <algorithm>
#include <cmath>
#include <regex>
#include <sstream>

#include "src/apib_rand.h"
#include "src/apib_time.h"
#include "src/apib_url.h"
#include "third_party/base64.h"

#define MAX_NUM_SIZE 256

namespace apib {

static const std::regex kAmpersand("&");
static const std::regex kEquals("=");

typedef std::pair<std::string, std::string> Param;
typedef std::vector<Param> ParamList;

/* Encode a string as described by the OAuth 1.0a spec, specifically
 * RFC5849. */
static void appendEncoded(std::ostream& o, const std::string& str) {
  size_t p = 0;
  while (str[p] != 0) {
    if (isalnum(str[p]) || (str[p] == '-') || (str[p] == '.') ||
        (str[p] == '_') || (str[p] == '~')) {
      o << str[p];

    } else {
      // Encode string to hexadecimal upper case!
      const int ch = str[p];
      o << '%' << std::hex << std::uppercase << ch << std::dec;
    }
    p++;
  }
}

static std::string reEncode(const std::string& str) {
  std::ostringstream s;
  appendEncoded(s, str);
  return s.str();
}

/*
 * Decode a string as described by the HTML spec and as commonly implemented. */
static std::string decode(const std::string& str) {
  std::ostringstream decoded;
  char tmp[3];

  for (size_t ip = 0; ip < str.size(); ip++) {
    const char c = str[ip];
    if (c == '+') {
      decoded << ' ';
    } else if (c == '%') {
      if ((ip + 2) >= str.size()) {
        // Bad input.
        break;
      }
      tmp[0] = str[ip + 1];
      tmp[1] = str[ip + 2];
      tmp[2] = 0;
      ip += 2;
      const int ch = std::stoi(tmp, nullptr, 16);
      decoded << (char)ch;
    } else {
      decoded << str[ip];
    }
  }
  return decoded.str();
}

static void readParams(ParamList& params, const std::string& str) {
  for (auto amp =
           std::sregex_token_iterator(str.cbegin(), str.cend(), kAmpersand, -1);
       amp != std::sregex_token_iterator(); amp++) {
    const std::string pp = *amp;
    auto eq = std::sregex_token_iterator(pp.begin(), pp.end(), kEquals, -1);
    if (eq != std::sregex_token_iterator()) {
      const std::string name = decode(*eq);
      std::string val;
      eq++;
      if (eq != std::sregex_token_iterator()) {
        val = decode(*eq);
      }
      params.push_back(std::make_pair(name, val));
    }
  }
}

static void addParam(ParamList& params, const std::string& name,
                     const std::string& val) {
  params.push_back(std::make_pair(name, val));
}

static bool compareParam(const Param& p1, const Param& p2) {
  if (p1.first < p2.first) {
    return true;
  }
  if (p1.first > p2.first) {
    return false;
  }
  return (p1.second < p2.second);
}

std::string oauth_generateHmac(const std::string& base,
                               const OAuthInfo& oauth) {
  std::ostringstream key;
  appendEncoded(key, oauth.consumerSecret);
  key << '&';
  if (!oauth.tokenSecret.empty()) {
    key << oauth.tokenSecret;
  }

  char hmac[EVP_MAX_MD_SIZE];
  unsigned int hmacLen;
  HMAC(EVP_sha1(), key.str().c_str(), key.str().size(),
       (unsigned char*)base.data(), base.size(), (unsigned char*)hmac,
       &hmacLen);

  char* ret = (char*)malloc(Base64encode_len(hmacLen));
  Base64encode(ret, hmac, hmacLen);
  return std::string(ret);
}

static std::string makeNonce(RandState rand) {
  long r1 = apib_Rand(rand);
  long r2 = apib_Rand(rand);
  std::ostringstream nonce;
  nonce << std::hex << std::uppercase << r1 << r2 << std::dec;
  return nonce.str();
}

std::string oauth_buildBaseString(RandState rand, const URLInfo& url,
                                  const std::string& method, long timestamp,
                                  const std::string& nonce,
                                  const char* sendData, size_t sendDataSize,
                                  const OAuthInfo& oauth) {
  std::ostringstream base;
  ParamList params;

  base << method << '&';

  /* Encoded and normalized URL */
  appendEncoded(base, (url.isSsl() ? URLInfo::kHttps : URLInfo::kHttp));
  appendEncoded(base, "://");
  appendEncoded(base, url.hostHeader());
  appendEncoded(base, url.pathOnly());

  /* Parse query */
  readParams(params, url.query());

  /* Parse form body */
  if (sendData != nullptr) {
    std::string tmpSend(sendData, sendDataSize);
    readParams(params, tmpSend);
  }

  /* Add additional OAuth params */
  if (!oauth.consumerKey.empty()) {
    addParam(params, "oauth_consumer_key", oauth.consumerKey);
  }
  if (!oauth.accessToken.empty()) {
    addParam(params, "oauth_token", oauth.accessToken);
  }
  addParam(params, "oauth_signature_method", "HMAC-SHA1");
  addParam(params, "oauth_nonce", nonce);
  addParam(params, "oauth_timestamp", std::to_string(timestamp));

  /* Re-encode each string! */
  for (size_t i = 0; i < params.size(); i++) {
    std::string newName = reEncode(params[i].first);
    std::string newVal = reEncode(params[i].second);
    params[i] = std::make_pair(newName, newVal);
  }

  /* Sort by name, then by value */
  std::sort(params.begin(), params.end(), compareParam);

  // Build the final list of params
  std::ostringstream finalParams;
  bool first = true;
  for (auto it = params.begin(); it != params.end(); it++) {
    if (first) {
      first = false;
    } else {
      finalParams << '&';
    }
    finalParams << it->first << '=' << it->second;
  }

  /* Attach them, which encodes them again */
  base << '&';
  appendEncoded(base, finalParams.str());
  return base.str();
}

std::string oauth_MakeQueryString(RandState rand, const URLInfo& url,
                                  const std::string& method,
                                  const char* sendData,
                                  unsigned int sendDataSize,
                                  const OAuthInfo& oauth) {
  const auto nonce = makeNonce(rand);
  const long timestamp = (long)floor(apib_Seconds(apib_GetTime()));
  const auto baseString = oauth_buildBaseString(
      rand, url, method, timestamp, nonce, sendData, sendDataSize, oauth);
  const auto hmac = oauth_generateHmac(baseString, oauth);

  /* Now generate the final query string */
  std::ostringstream query;
  query << "oauth_consumer_key=";
  appendEncoded(query, oauth.consumerKey);
  if (!oauth.accessToken.empty()) {
    query << "&oauth_token=";
    appendEncoded(query, oauth.accessToken);
  }
  query << "&oauth_signature_method=HMAC-SHA1";
  query << "&oauth_signature=";
  appendEncoded(query, hmac);
  query << "&oauth_timestamp=" << timestamp;
  query << "&oauth_nonce=" << nonce;
  return query.str();
}

std::string oauth_MakeHeader(RandState rand, const URLInfo& url,
                             const std::string& realm,
                             const std::string& method, const char* sendData,
                             unsigned int sendDataSize,
                             const OAuthInfo& oauth) {
  long timestamp = (long)floor(apib_Seconds(apib_GetTime()));
  const auto nonce = makeNonce(rand);

  const auto baseString = oauth_buildBaseString(
      rand, url, method, timestamp, nonce, sendData, sendDataSize, oauth);
  const auto hmac = oauth_generateHmac(baseString, oauth);

  /* Now generate the final query string */
  std::ostringstream hdr;
  hdr << "Authorization: OAuth realm=\"" << realm
      << "\", oauth_consumer_key=\"";
  appendEncoded(hdr, oauth.consumerKey);
  if (!oauth.accessToken.empty()) {
    hdr << "\", oauth_token=\"";
    appendEncoded(hdr, oauth.accessToken);
  }
  hdr << "\", oauth_signature_method=\"HMAC-SHA1\", oauth_signature=\"";
  appendEncoded(hdr, hmac);
  hdr << "\", oauth_timestamp=\"" << timestamp << "\", oauth_nonce=\"" << nonce
      << '\"';
  return hdr.str();
}

}  // namespace apib