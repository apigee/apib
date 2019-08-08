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

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Code for URL handling
 */

typedef struct {
  apr_uri_t        url;
  apr_sockaddr_t**  addresses;
  int              addressCount;
  int              port;
  int              isSsl;
} URLInfo;

/*
 * Get a randomly-selected URL, plus address and port, for the next
 * request. This allows us to balance requests over many separate URLs.
 */

extern const URLInfo* url_GetNext(RandState rand);

/*
 * Get the network address for the next request based on which connection is making it.
 * Using the index number for each connection means that for a host with multiple IPs, we
 * evenly distribute requests across them without opening a new connection for each request.
 */

extern apr_sockaddr_t* url_GetAddress(const URLInfo* url, int index);

/*
 * Set the following as the one and only one URL for this session.
 */
extern int url_InitOne(const char* urlStr, apr_pool_t* pool);

/*
 * Read a list of URLs from a file, one line per URL.
 */
extern int url_InitFile(const char* fileName, apr_pool_t* pool);

/* 
 * Return whether the two URLs refer to the same host and port for the given connection --
 * we use this to optimize socket management.
 */
extern int url_IsSameServer(const URLInfo* u1, const URLInfo* u2, int index);

/*
 * Create an APR random number generator. It is not thread-safe so we are 
 * going to create a few.
 */
extern void url_InitRandom(RandState rand);

#ifdef __cplusplus
}
#endif

#endif  // APIB_URL_H
