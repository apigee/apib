/*
Copyright 2020 Google LLC

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

#include "apib/addresses.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/ip.h>

#include <cassert>
#include <cerrno>
#include <cstring>

namespace apib {

static const int kNumericAddressLen = 64;

Address::Address() { address_.ss_family = AF_UNSPEC; }

Address::Address(const struct sockaddr* addr, socklen_t len) {
  memcpy(&address_, addr, len);
}

Address::Address(const struct sockaddr* addr, socklen_t len, uint16_t port) {
  memcpy(&address_, addr, len);
  setPort(port);
}

socklen_t Address::get(struct sockaddr_storage* addr) const {
  assert(addr != nullptr);
  const socklen_t copylen = length();
  memcpy(addr, &address_, copylen);
  return copylen;
}

socklen_t Address::length() const {
  switch (address_.ss_family) {
    case AF_INET:
      return sizeof(struct sockaddr_in);
    case AF_INET6:
      return sizeof(struct sockaddr_in6);
    case AF_UNSPEC:
      return 0;
    default:
      assert(0);
      return 0;
  }
}

uint16_t Address::port() const {
  switch (address_.ss_family) {
    case AF_INET:
      return ntohs(((struct sockaddr_in*)&address_)->sin_port);
    case AF_INET6:
      return ntohs(((struct sockaddr_in6*)&address_)->sin6_port);
      break;
    case AF_UNSPEC:
      return 0;
    default:
      assert(0);
      return 0;
  }
}

void Address::setPort(uint16_t port) {
  switch (address_.ss_family) {
    case AF_INET:
      ((struct sockaddr_in*)&address_)->sin_port = htons(port);
      break;
    case AF_INET6:
      ((struct sockaddr_in6*)&address_)->sin6_port = htons(port);
      break;
    default:
      assert(0);
      break;
  }
}

bool operator==(const Address& a1, const Address& a2) {
  if (a1.address_.ss_family != a2.address_.ss_family) {
    return false;
  }
  return !memcmp(&a1.address_, &a2.address_, a1.length());
}

std::string Address::str() const {
  char nameBuf[kNumericAddressLen];
  const int s =
      ::getnameinfo((const struct sockaddr*)&address_, length(), nameBuf,
                    kNumericAddressLen, nullptr, 0, NI_NUMERICHOST);
  if (s == 0) {
    return nameBuf;
  }
  // Fail silently since this is usually for information purposes
  return "";
}

StatusOr<AddressesPtr> Addresses::lookup(absl::string_view name, int family) {
  struct addrinfo hints;
  memset(&hints, 0, sizeof(struct ::addrinfo));
  hints.ai_family = family;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_flags = AI_ADDRCONFIG | AI_V4MAPPED;

  struct addrinfo* results;
  const std::string nameStr(name);
  const int s = ::getaddrinfo(nameStr.c_str(), nullptr, &hints, &results);
  if (s == EAI_SYSTEM) {
    return Status(Status::SOCKET_ERROR, errno);
  }
  if (s != 0) {
    return Status(Status::DNS_ERROR, gai_strerror(s));
  }

  Addresses* ret = new Addresses(results);
  freeaddrinfo(results);
  return AddressesPtr(ret);
}

Addresses::Addresses(struct addrinfo* addrs) {
  struct addrinfo* a = addrs;
  while (a != nullptr) {
    addresses_.push_back(Address(a->ai_addr, a->ai_addrlen, 0));
    a = a->ai_next;
  }
}

Address Addresses::get(uint16_t port, int sequence) const {
  if (addresses_.empty()) {
    // Empty address, with "AF_UNSPEC" family.
    return Address();
  }
  const int ix = sequence % addresses_.size();
  Address ret = addresses_[ix];
  ret.setPort(port);
  return ret;
}

}  // namespace apib
