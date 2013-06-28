/*
   Copyright 2013 Apigee Corp.

   Licensed under the Apache License, Version 2.0 (the "License");
   you may not use this file except in compliance with the License.
   You may obtain a copy of the License at

       http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software
   distributed under the License is distributed on an "AS IS" BASIS,
   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
   See the License for the specific language governing permissions and
   limitations under the License.
*/

#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>

#include <apib_common.h>

int main(int argc, char** argv)
{
  struct addrinfo* result;
  struct addrinfo* r;
  int err;
  apr_pool_t* pool;
  apr_sockaddr_t* aprAddr;
  apr_sockaddr_t* a;
  char errBuf[128];

  apr_initialize();
  apr_pool_create(&pool, NULL);

  if (argc != 2) {
    fprintf(stderr, "Usage: apib_dnstest <hostname>\n");
    return 2;
  }
  
  err = apr_sockaddr_info_get(&aprAddr, argv[1], APR_UNSPEC, 0, 0, pool);
  if (err != APR_SUCCESS) {
    apr_strerror(err, errBuf, 128);
    printf("Error: %s\n", errBuf);
    return 3;
  }

  a = aprAddr;
  while (a != NULL) {
    char* addrStr;

    apr_sockaddr_ip_get(&addrStr, a);
    printf("%s\n", addrStr);
    a = a->next;
  }

  /*  err = getaddrinfo(argv[1], NULL, NULL, &result);
  if (err != 0) {
    perror("DNS lookup error");
    return 3;
  }

  r = result;
  while (r != NULL) {
    apr_sockaddr_t addr;
    char* addrStr;

    addr.pool = pool;
    addr.hostname = addr.servname = NULL;
    addr.port = 0;
    addr.family = r->ai_family;
    addr.salen = 0;
    addr.ipaddr_len = r->ai_addrlen;
    addr.addr_str_len = 0;
    addr.ipaddr_ptr = r->ai_addr;
    addr.next = NULL;

    apr_sockaddr_ip_get(&addrStr, &addr);
    printf("%s\n", addrStr);
    r = r->ai_next;
  }

  freeaddrinfo(result);
  */

  apr_pool_destroy(pool);
  apr_terminate();
  return 0;
}
