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

#include "apib_url.h"

#include <arpa/inet.h>
#include <assert.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>

#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>

#include "http_parser.h"
#include "src/apib_lines.h"
#include "src/apib_util.h"

#define URL_BUF_LEN 8192
#define INITIAL_URLS 16

using std::cerr;
using std::cout;
using std::endl;

namespace apib {

std::vector<URLInfo> URLInfo::urls_;
bool URLInfo::initialized_ = false; 
const std::string URLInfo::kHttp = "http";
const std::string URLInfo::kHttps = "https";

URLInfo::URLInfo() {}

URLInfo::URLInfo(const URLInfo& u) {
  port_ = u.port_;
  isSsl_ = u.isSsl_;
  path_ = u.path_;
  pathOnly_ = u.pathOnly_;
  query_ = u.query_;
  hostName_ = u.hostName_;
  hostHeader_ = u.hostHeader_;

  for (size_t i = 0; i < u.addresses_.size(); i++) {
    const auto alen = u.addressLengths_[i];
    struct sockaddr* addr = (struct sockaddr*)malloc(alen);
    memcpy(addr, u.addresses_[i], alen);
    addresses_.push_back(addr);
    addressLengths_.push_back(alen);
  }
}

URLInfo::~URLInfo() {
  for (auto it = addresses_.begin(); it != addresses_.end(); it++) {
    free(*it);
  }
}

int URLInfo::initHost(const std::string& hostname) {
  struct addrinfo hints;
  struct addrinfo* results;

  // For now, look up only IP V4 addresses
  memset(&hints, 0, sizeof(struct addrinfo));
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_protocol = 0;
  hints.ai_flags = 0;

  const int addrerr = getaddrinfo(hostname.c_str(), NULL, &hints, &results);
  if (addrerr) {
    return -1;
  }

  // Copy results to vector
  struct addrinfo* a = results;
  while (a != NULL) {
    struct sockaddr* addr = (struct sockaddr*)malloc(a->ai_addrlen);
    memcpy(addr, a->ai_addr, a->ai_addrlen);
    // IP4 and IP6 versions of this should have port in same place
    ((struct sockaddr_in*)addr)->sin_port = htons(port_);
    addresses_.push_back(addr);
    addressLengths_.push_back(a->ai_addrlen);
    a = a->ai_next;
  }
  freeaddrinfo(results);

  return 0;
}

static std::string urlPart(const struct http_parser_url* pu,
                           const std::string& urlstr, int part) {
  return urlstr.substr(pu->field_data[part].off, pu->field_data[part].len);
}

int URLInfo::init(const std::string& urlstr) {
  struct http_parser_url pu;

  http_parser_url_init(&pu);
  const int err = http_parser_parse_url(urlstr.data(), urlstr.size(), 0, &pu);
  if (err != 0) {
    cerr << "Invalid URL: \"" << urlstr << '\"' << endl;
    return -1;
  }

  if (!(pu.field_set & (1 << UF_SCHEMA)) || !(pu.field_set & (1 << UF_HOST))) {
    cerr << "Error matching URL: " << urlstr << endl;
    return -2;
  }

  if (kHttp == urlPart(&pu, urlstr, UF_SCHEMA)) {
    isSsl_ = false;
  } else if (kHttps == urlPart(&pu, urlstr, UF_SCHEMA)) {
    isSsl_ = true;
  } else {
    cerr << "Invalid URL scheme: \"" << urlstr << '\"' << endl;
    return -3;
  }

  hostName_ = urlPart(&pu, urlstr, UF_HOST);

  if (pu.field_set & (1 << UF_PORT)) {
    port_ = pu.port;
  } else if (isSsl_) {
    port_ = 443;
  } else {
    port_ = 80;
  }

  std::ostringstream path;

  if (pu.field_set & (1 << UF_PATH)) {
    const auto p = urlPart(&pu, urlstr, UF_PATH);
    path << p;
    pathOnly_ = p;
  } else {
    path << '/';
    pathOnly_ = "/";
  }

  if (pu.field_set & (1 << UF_QUERY)) {
    const auto q = urlPart(&pu, urlstr, UF_QUERY);
    path << '?' << q;
    query_ = q;
  }

  if (pu.field_set & (1 << UF_FRAGMENT)) {
    const auto f = urlPart(&pu, urlstr, UF_FRAGMENT);
    path << '#' << f;
  }

  // Copy the final buffer...
  path_ = path.str();

  // Calculate the host header properly
  if ((isSsl_ && (port_ == 443)) || (!isSsl_ && (port_ == 80))) {
    hostHeader_ = hostName_;
  } else {
    std::ostringstream hh;
    hh << hostName_ << ':' << port_;
    hostHeader_ = hh.str();
  }

  // Now look up the host and add the port...
  // It's OK if it fails now!
  initHost(hostName_);

  return 0;
}

int URLInfo::InitOne(const std::string& urlStr) {
  assert(!initialized_);
  URLInfo url;
  const int e = url.init(urlStr);
  urls_.push_back(url);
  if (e == 0) {
    initialized_ = true;
  }
  return e;
}

struct sockaddr* URLInfo::address(int index, size_t* len) const {
  if (addresses_.empty()) {
    return nullptr;
  }
  assert(addresses_.size() == addressLengths_.size());
  const int ix = index % addresses_.size();
  if (len != nullptr) {
    *len = addressLengths_[ix];
  }
  return addresses_[ix];
}

bool URLInfo::IsSameServer(const URLInfo& u1, const URLInfo& u2, int index) {
  if (u1.addresses_.size() != u2.addresses_.size()) {
    return false;
  }
  const int ix = index % u1.addresses_.size();
  if (u1.addressLengths_[ix] != u2.addressLengths_[ix]) {
    return false;
  }
  return !memcmp(u1.addresses_[ix], u2.addresses_[ix], u1.addressLengths_[ix]);
}

int URLInfo::InitFile(const std::string& fileName) {
  assert(!initialized_);

  std::ifstream in(fileName);
  if (in.fail()) {
    cerr << "Can't open \"" << fileName << '\"' << endl;
    return -1;
  }

  LineState line(URL_BUF_LEN);

  int rc = line.readStream(in);
  if (rc < 0) {
    return -1;
  }

  do {
    while (line.next()) {
      const auto urlStr = line.line();
      URLInfo u;
      int err = u.init(urlStr);
      if (err) {
        cerr << "Invalid URL \"" << urlStr << '\"' << endl;
        return -1;
      }
      urls_.push_back(u);
    }
    line.consume();
    rc = line.readStream(in);
  } while (rc > 0);

  cout << "Read " << urls_.size() << " URLs from \"" << fileName << '\"'
       << endl;

  initialized_ = true;
  return 0;
}

URLInfo* const URLInfo::GetNext(RandomGenerator* rand) {
  if (urls_.empty()) {
    return nullptr;
  }
  if (urls_.size() == 1) {
    return &(urls_[0]);
  }

  const int32_t ix = rand->get(0, (urls_.size() - 1));
  return &(urls_[ix]);
}

void URLInfo::Reset() {
  urls_.clear();
  initialized_ = false;
}

}  // namespace apib