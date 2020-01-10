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

#ifndef APIB_ADDRESSES_H
#define APIB_ADDRESSES_H

#include <memory>
#include <ostream>
#include <string>
#include <vector>

// clang-format off
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
// clang-format on

#include "absl/strings/string_view.h"
#include "apib/status.h"

namespace apib {

class Address {
 public:
  // The default address has the AF_UNSPEC family.
  Address();
  // Create the specified address as-is
  Address(const struct sockaddr* addr, socklen_t len);
  // Create the specified address and stamp it with a port
  Address(const struct sockaddr* addr, socklen_t len, uint16_t port);
  Address(const Address&) = default;
  Address& operator=(const Address&) = default;

  // Return the length of the address and copy it to "addr"
  socklen_t get(struct sockaddr_storage* addr) const;

  bool valid() const { return (address_.ss_family != AF_UNSPEC); }
  int family() const { return address_.ss_family; }
  socklen_t length() const;
  uint16_t port() const;
  void setPort(uint16_t port);
  std::string str() const;

  friend bool operator==(const Address& a1, const Address& a2);

  friend std::ostream& operator<<(std::ostream& out, const Address& s) {
    out << s.str();
    return out;
  }

 private:
  struct sockaddr_storage address_;
};

class Addresses;
typedef std::unique_ptr<Addresses> AddressesPtr;

/*
 * This class abstracts out a list of network addresses from DNS.
 */
class Addresses {
 public:
  Addresses() {}
  Addresses(const Addresses&) = delete;
  Addresses& operator=(const Addresses&) = delete;

  // Look up all the addresses returned by DNS and return an object that
  // holds them. "family" may be used to choose IP4 or 6 if desired.
  static StatusOr<AddressesPtr> lookup(absl::string_view name,
                                       int family = AF_UNSPEC);

  // Get the actual address, and stamp the port number on it.
  // The optional "sequence" parameter lets a caller
  // round-robin through all the possible addresses.
  Address get(uint16_t port, int sequence = 0) const;

  int size() const { return addresses_.size(); }

 private:
  Addresses(struct addrinfo* addrs);

  std::vector<Address> addresses_;
};

}  // namespace apib

#endif
