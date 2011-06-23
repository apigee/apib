#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <apr_strings.h>
#include <apr_uri.h>

#include <openssl/ssl.h>

#include <apib_common.h>

int main(int argc, char** argv)
{
  apr_pool_t* pool;
  char* consumerKey;
  char* consumerSecret;
  char* accessToken;
  char* tokenSecret;
  char* last;
  apr_uri_t uri;

  if (argc != 3) {
    fprintf(stderr, "Usage: %s <URL> <oauth spec>\n", argv[0]);
    fprintf(stderr, "OAuth spec is:\n");
    fprintf(stderr, "consumer key:secret:access token:secret\n");
    return 2;
  }

  apr_initialize();
  apr_pool_create(&pool, NULL);
  SSL_library_init();
  consumerKey = apr_strtok(argv[2], ":", &last);
  consumerSecret = apr_strtok(NULL, ":", &last);
  accessToken = apr_strtok(NULL, ":", &last);
  tokenSecret = apr_strtok(NULL, ":", &last);

  apr_uri_parse(pool, argv[1], &uri);

  printf("%s\n", 
	 oauth_MakeAuthorization(&uri, "GET", NULL, 0,
				 consumerKey, consumerSecret,
				 accessToken, tokenSecret,
				 pool));
  printf("%s\n", 
	 oauth_MakeQueryString(&uri, "GET", NULL, 0,
			       consumerKey, consumerSecret,
			       accessToken, tokenSecret,
			       pool));
  
  apr_pool_destroy(pool);
  apr_terminate();
}
