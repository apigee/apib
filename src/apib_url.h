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

#include "src/apib_rand.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
Code for URL handling. The list of URLs is global. It's
assumed that code will call either "url_InitFile" or
"url_InitOne", and then call "GetNext" and "GetAddress"
for each invocation. There's no locking because the "Get"
functions don't change state.
*/

typedef struct {
  struct sockaddr_storage* addresses;
  size_t* addressLengths;
  int addressCount;
  unsigned int port;
  int isSsl;
  char* path;
  char* pathOnly;
  char* query;
  char* hostName;
  char* hostHeader;
} URLInfo;

/*
 * Set the following as the one and only one URL for this session.
 */
extern int url_InitOne(const char* urlStr);

/*
 * Read a list of URLs from a file, one line per URL.
 */
extern int url_InitFile(const char* fileName);

/*
 * Clear the effects of one of the Init functions. This is helpful
 * in writing tests.
 */
extern void url_Reset();

/*
 * Get a randomly-selected URL, plus address and port, for the next
 * request. This allows us to balance requests over many separate URLs.
 */
extern URLInfo* url_GetNext(RandState rand);

/*
 * Get the network address for the next request based on which connection is
 * making it. Using the index number for each connection means that for a host
 * with multiple IPs, we evenly distribute requests across them without opening
 * a new connection for each request.
 */
extern struct sockaddr* url_GetAddress(const URLInfo* url, int index, size_t* len);

/*
 * Return whether the two URLs refer to the same host and port for the given
 * connection -- we use this to optimize socket management.
 */
extern int url_IsSameServer(const URLInfo* u1, const URLInfo* u2, int index);

#ifdef __cplusplus
}
#endif

#endif  // APIB_URL_H
