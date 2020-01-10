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

#ifndef APIB_URL_H
#define APIB_URL_H

#include <sys/socket.h>
#include <sys/types.h>

#include <memory>
#include <string>

#include "absl/strings/string_view.h"
#include "apib/addresses.h"
#include "apib/apib_rand.h"
#include "apib/status.h"

namespace apib {

class URLInfo;
typedef std::unique_ptr<URLInfo> URLInfoPtr;

/*
Code for URL handling. The list of URLs is global. It's
assumed that code will call either "url_InitFile" or
"url_InitOne", and then call "GetNext" and "GetAddress"
for each invocation. There's no locking because the "Get"
functions don't change state.
*/
class URLInfo {
 public:
  static const std::string kHttp;
  static const std::string kHttps;

  URLInfo() {}
  URLInfo(const URLInfo&) = delete;
  URLInfo& operator=(const URLInfo&) = delete;

  /*
   * Set the following as the one and only one URL for this session.
   * There will not be an error returned if DNS lookup fails, but
   * "lookupStatus()" may be queried in that case.
   */
  static Status InitOne(absl::string_view urlStr);

  /*
   * Read a list of URLs from a file, one line per URL.
   */
  static Status InitFile(absl::string_view fileName);

  /*
   * Clear the effects of one of the Init functions. This is helpful
   * in writing tests.
   */
  static void Reset();

  /*
   * Get a randomly-selected URL, plus address and port, for the next
   * request. This allows us to balance requests over many separate URLs.
   */
  static URLInfo* const GetNext(RandomGenerator* rand);

  /*
   * Return whether the two URLs refer to the same host and port for the given
   * connection -- we use this to optimize socket management.
   */
  static bool IsSameServer(const URLInfo& u1, const URLInfo& u2, int sequence);

  /*
   * Get the network address for the next request based on which connection is
   * making it. Using the sequence number for each connection means that for a
   * host with multiple IPs, we evenly distribute requests across them without
   * opening a new connection for each request.
   */
  Address address(int sequence) const {
    return addresses_->get(port_, sequence);
  }

  uint16_t port() const { return port_; }
  bool isSsl() const { return isSsl_; }
  std::string path() const { return path_; }
  std::string pathOnly() const { return pathOnly_; }
  std::string query() const { return query_; }
  std::string hostName() const { return hostName_; }
  std::string hostHeader() const { return hostHeader_; }
  size_t addressCount() const { return addresses_->size(); }
  Status lookupStatus() const { return lookupStatus_; }

 private:
  Status init(absl::string_view urlStr);

  uint16_t port_;
  bool isSsl_;
  std::string path_;
  std::string pathOnly_;
  std::string query_;
  std::string hostName_;
  std::string hostHeader_;
  Status lookupStatus_;
  AddressesPtr addresses_;

  static std::vector<URLInfoPtr> urls_;
  static bool initialized_;
};

}  // namespace apib

#endif  // APIB_URL_H
