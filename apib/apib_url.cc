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
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>

#include <cassert>
#include <cstring>
#include <fstream>
#include <iostream>

#include "absl/strings/str_cat.h"
#include "apib/apib_lines.h"
#include "apib/apib_util.h"
#include "http_parser.h"

#define URL_BUF_LEN 8192
#define INITIAL_URLS 16

using std::cerr;
using std::cout;
using std::endl;

namespace apib {

std::vector<URLInfoPtr> URLInfo::urls_;
bool URLInfo::initialized_ = false;
const std::string URLInfo::kHttp = "http";
const std::string URLInfo::kHttps = "https";

/*
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
*/

static absl::string_view urlPart(const struct http_parser_url* pu,
                                 const absl::string_view urlstr, int part) {
  return urlstr.substr(pu->field_data[part].off, pu->field_data[part].len);
}

Status URLInfo::init(absl::string_view urlstr) {
  struct http_parser_url pu;

  http_parser_url_init(&pu);
  const int err = http_parser_parse_url(urlstr.data(), urlstr.size(), 0, &pu);
  if (err != 0) {
    return Status(Status::INVALID_URL, urlstr);
  }

  if (!(pu.field_set & (1 << UF_SCHEMA)) || !(pu.field_set & (1 << UF_HOST))) {
    return Status(Status::INVALID_URL, urlstr);
  }

  if (kHttp == urlPart(&pu, urlstr, UF_SCHEMA)) {
    isSsl_ = false;
  } else if (kHttps == urlPart(&pu, urlstr, UF_SCHEMA)) {
    isSsl_ = true;
  } else {
    return Status(Status::INVALID_URL, "Invalid scheme");
  }

  hostName_ = std::string(urlPart(&pu, urlstr, UF_HOST));

  if (pu.field_set & (1 << UF_PORT)) {
    port_ = pu.port;
  } else if (isSsl_) {
    port_ = 443;
  } else {
    port_ = 80;
  }

  if (pu.field_set & (1 << UF_PATH)) {
    pathOnly_ = std::string(urlPart(&pu, urlstr, UF_PATH));
  } else {
    pathOnly_ = "/";
  }
  path_ = pathOnly_;

  if (pu.field_set & (1 << UF_QUERY)) {
    query_ = std::string(urlPart(&pu, urlstr, UF_QUERY));
    absl::StrAppend(&path_, "?", query_);
  }

  if (pu.field_set & (1 << UF_FRAGMENT)) {
    const auto f = urlPart(&pu, urlstr, UF_FRAGMENT);
    absl::StrAppend(&path_, "#", f);
  }

  // Calculate the host header properly
  if ((isSsl_ && (port_ == 443)) || (!isSsl_ && (port_ == 80))) {
    hostHeader_ = hostName_;
  } else {
    hostHeader_ = absl::StrCat(hostName_, ":", port_);
  }

  auto ls = Addresses::lookup(hostName_);
  lookupStatus_ = ls.status();
  if (ls.ok()) {
    ls.valueptr()->swap(addresses_);
  } else {
    // Insert an empty vector of addresses.
    addresses_.reset(new Addresses());
  }

  return Status::kOk;
}

Status URLInfo::InitOne(absl::string_view urlStr) {
  assert(!initialized_);
  URLInfoPtr url(new URLInfo());
  const auto s = url->init(urlStr);
  if (s.ok()) {
    urls_.push_back(std::move(url));
    initialized_ = true;
  }
  return s;
}

bool URLInfo::IsSameServer(const URLInfo& u1, const URLInfo& u2, int sequence) {
  const Address a1 = u1.address(sequence);
  const Address a2 = u2.address(sequence);
  return a1 == a2;
}

Status URLInfo::InitFile(absl::string_view fileName) {
  assert(!initialized_);

  std::ifstream in(std::string(fileName).c_str());
  if (in.fail()) {
    return Status(Status::IO_ERROR, fileName);
  }

  LineState line(URL_BUF_LEN);

  int rc = line.readStream(in);
  if (rc < 0) {
    return Status(Status::IO_ERROR);
  }

  do {
    while (line.next()) {
      const auto urlStr = line.line();
      URLInfoPtr u(new URLInfo());
      const auto s = u->init(urlStr);
      if (!s.ok()) {
        return s;
      }
      urls_.push_back(std::move(u));
    }
    line.consume();
    rc = line.readStream(in);
  } while (rc > 0);

  cout << "Read " << urls_.size() << " URLs from \"" << fileName << '\"'
       << endl;

  initialized_ = true;
  return Status::kOk;
}

URLInfo* const URLInfo::GetNext(RandomGenerator* rand) {
  if (urls_.empty()) {
    return nullptr;
  }
  if (urls_.size() == 1) {
    return urls_[0].get();
  }

  const int32_t ix = rand->get(0, (urls_.size() - 1));
  return urls_[ix].get();
}

void URLInfo::Reset() {
  urls_.clear();
  initialized_ = false;
}

}  // namespace apib