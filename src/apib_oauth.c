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

#include <assert.h>
#include <ctype.h>
#include <openssl/bio.h>
#include <openssl/hmac.h>
#include <openssl/sha.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "src/apib_oauth.h"
#include "src/apib_lines.h"
#include "src/apib_rand.h"
#include "src/apib_time.h"
#include "src/apib_url.h"
#include "third_party/base64.h"

#define MAX_NUM_SIZE 256

typedef struct {
  char* name;
  char* val;
} Param;

typedef struct {
  Param* params;
  size_t len;
  size_t size;
} Params;

static void allocParams(Params* p, size_t size) {
  p->len = 0;
  p->size = size;
  p->params = (Param*)malloc(sizeof(Param) * p->size);
}

static void freeParams(Params* p) {
  for (size_t i = 0; i < p->len; i++) {
    Param* pp = &(p->params[i]);
    if (pp->name != NULL) {
      free(pp->name);
    }
    if (pp->val != NULL) {
      free(pp->val);
    }
  }
  free(p->params);
}

/* Encode a string as described by the OAuth 1.0a spec, specifically
 * RFC5849. */
static void appendEncoded(StringBuf* b, const char* str) {
  size_t p = 0;
  while (str[p] != 0) {
    if (isalnum(str[p]) || (str[p] == '-') || (str[p] == '.') ||
        (str[p] == '_') || (str[p] == '~')) {
      buf_AppendChar(b, str[p]);

    } else {
      buf_Printf(b, "%%%02X", str[p]);
    }
    p++;
  }
}

static char* reEncode(const char* str) {
  StringBuf b;
  buf_New(&b, strlen(str));
  appendEncoded(&b, str);
  return buf_Get(&b);
}

/*
 * Decode a string as described by the HTML spec and as commonly implemented. */
static char* decode(const char* str) {
  char* ret =  malloc(strlen(str) + 1);
  size_t ip = 0;
  size_t op = 0;
  char buf[4];

  while (str[ip] != 0) {
    if (str[ip] == '+') {
      ret[op] = ' ';
    } else if (str[ip] == '%') {
      buf[0] = str[ip + 1];
      buf[1] = str[ip + 2];
      buf[2] = 0;
      if ((buf[0] == 0) || (buf[1] == 0)) {
        /* Bad input. */
        break;
      }
      ip += 2;
      ret[op] = (char)strtol(buf, 0, 16);

    } else {
      ret[op] = str[ip];
    }
    ip++;
    op++;
  }
  ret[op] = 0;
  return ret;
}

static void ensureParamsSize(Params* params) {
  if (params->len >= params->size) {
    params->size *= 2;
    params->params = (Param*)realloc(params->params, sizeof(Param) * params->size);
  }
}

static void readParams(Params* params, char* str, size_t len) {
  char* tok;
  char* last;

  tok = strtok_r(str, "&", &last);
  while (tok != NULL) {
    char* param = tok;
    char* tok2;
    char* last2;

    ensureParamsSize(params);
    tok2 = strtok_r(param, "=", &last2);
    params->params[params->len].name = decode(tok2);
    tok2 = strtok_r(NULL, "=", &last2);
    if (tok2 == NULL) {
      params->params[params->len].val = NULL;
    } else {
      params->params[params->len].val = decode(tok2);
    }

    params->len++;

    tok = strtok_r(NULL, "&", &last);
  }
}


static void addParam(Params* params, const char* name, const char* val) {
  ensureParamsSize(params);
  params->params[params->len].name = strdup(name);
  params->params[params->len].val = strdup(val);
  params->len++;
}

static int compareParam(const void* v1, const void* v2) {
  const Param* p1 = (const Param*)v1;
  const Param* p2 = (const Param*)v2;

  int c = strcmp(p1->name, p2->name);
  if (c == 0) {
    if ((p1->val == NULL) && (p2->val == NULL)) {
      return 0;
    }
    if ((p1->val == NULL) && (p2->val != NULL)) {
      return -1;
    }
    if ((p1->val != NULL) && (p2->val == NULL)) {
      /* Yes I know I could have eliminated one == there! */
      return 1;
    }
    return strcmp(p1->val, p2->val);
  }
  return c;
}

char* oauth_generateHmac(const char* base, const OAuthInfo* oauth) {
  StringBuf keyBuf;

  buf_New(&keyBuf, 0);
  appendEncoded(&keyBuf, oauth->consumerSecret);
  buf_AppendChar(&keyBuf, '&');
  if (oauth->tokenSecret != NULL) {
    appendEncoded(&keyBuf, oauth->tokenSecret);
  }

  char hmac[EVP_MAX_MD_SIZE];
  unsigned int hmacLen;
  HMAC(EVP_sha1(), buf_Get(&keyBuf), buf_Length(&keyBuf), 
       (unsigned char*)base, strlen(base), (unsigned char*)hmac, &hmacLen);

  char* ret = malloc(Base64encode_len(hmacLen));
  Base64encode(ret, hmac, hmacLen);

  buf_Free(&keyBuf);
  return ret;
}

static void makeNonce(RandState rand, char* buf, size_t len) {
  long r1 = apib_Rand(rand);
  long r2 = apib_Rand(rand);
  const int err = snprintf(buf, len, "%lx%lx", r1, r2);
  assert(err < len);
}

char* oauth_buildBaseString(RandState rand, const URLInfo* url,
                            const char* method, 
                            long timestamp, const char* nonce,
                            const char* sendData,
                            size_t sendDataSize, const OAuthInfo* oauth) {

  StringBuf buf;  
  Params params;                          

  buf_New(&buf, 0);
  allocParams(&params, 8);

  buf_Append(&buf, method);
  buf_AppendChar(&buf, '&');

  /* Encoded and normalized URL */
  appendEncoded(&buf, (url->isSsl ? "https" : "http"));
  appendEncoded(&buf, "://");
  appendEncoded(&buf, url->hostHeader);
  appendEncoded(&buf, url->pathOnly);

  /* Parse query */
  if (url->query != NULL) {
    char* tmpQuery = strdup(url->query);
    readParams(&params, tmpQuery, strlen(tmpQuery));
    free(tmpQuery);
  }

  /* Parse form body */
  if (sendData != NULL) {
    char* tmpSend = strndup(sendData, sendDataSize);
    readParams(&params, tmpSend, sendDataSize);
    free(tmpSend);
  }

  /* Add additional OAuth params */
  if (oauth->consumerKey != NULL) {
    addParam(&params, "oauth_consumer_key", oauth->consumerKey);
  }
  if (oauth->accessToken != NULL) {
    addParam(&params, "oauth_token", oauth->accessToken);
  }
  addParam(&params, "oauth_signature_method", "HMAC-SHA1");
  addParam(&params, "oauth_nonce", nonce);

  char ts[MAX_NUM_SIZE];
  const int err = snprintf(ts, MAX_NUM_SIZE, "%li", timestamp);
  assert(err < MAX_NUM_SIZE);
  addParam(&params, "oauth_timestamp", ts);

  /* Re-encode each string! */
  for (unsigned int inc = 0; inc < params.len; inc++) {
    Param* p = &(params.params[inc]);
    char* reName = reEncode(p->name);
    free(p->name);
    p->name = reName;
    char* reVal;
    if (p->val != NULL) {
      reVal = reEncode(p->val);
      free(p->val);
      p->val = reVal;
    } else {
      reVal = "";
    }
  }

  /* Sort by name, then by value */
  qsort(params.params, params.len, sizeof(Param), compareParam);

  StringBuf paramBuf;
  buf_New(&paramBuf, 0);
  for (unsigned int inc = 0; inc < params.len; inc++) {
    if (inc > 0) {
      buf_AppendChar(&paramBuf, '&');
    }
    buf_Append(&paramBuf, params.params[inc].name);
    buf_AppendChar(&paramBuf, '=');
    if (params.params[inc].val != NULL) {
      buf_Append(&paramBuf, params.params[inc].val);
    }
  }

  /* Attach them, which encodes them again */
  buf_AppendChar(&buf, '&');
  appendEncoded(&buf, buf_Get(&paramBuf));
  buf_Free(&paramBuf);

  freeParams(&params);
  
  return buf_Get(&buf);
}

char* oauth_MakeQueryString(RandState rand, const URLInfo* url, const char* method,
                            const char* sendData, unsigned int sendDataSize,
                            const OAuthInfo* oauth) {

  long timestamp = (long)floor(apib_Seconds(apib_GetTime()));
  char nonce[MAX_NUM_SIZE];
  makeNonce(rand, nonce, MAX_NUM_SIZE);

  char* baseString = oauth_buildBaseString(rand, url, method, timestamp, nonce, sendData, sendDataSize, oauth);
  char* hmac = oauth_generateHmac(baseString, oauth);

  /* Now generate the final query string */
  StringBuf buf;
  buf_New(&buf, 0);

  buf_Append(&buf, "oauth_consumer_key=");
  appendEncoded(&buf, oauth->consumerKey);
  if (oauth->accessToken != NULL) {
    buf_Append(&buf, "&oauth_token=");
    appendEncoded(&buf, oauth->accessToken);
  }
  buf_Append(&buf, "&oauth_signature_method=HMAC-SHA1");
  buf_Append(&buf, "&oauth_signature=");
  appendEncoded(&buf, hmac);
  buf_Printf(&buf, "&oauth_timestamp=%li", timestamp);
  buf_Append(&buf, "&oauth_nonce=");
  buf_Append(&buf, nonce);

  free(baseString);
  free(hmac);

  // Caller frees the final buffer
  return buf_Get(&buf);
}

/*
char* oauth_MakeAuthorization(const apr_uri_t* url, const char* method,
                              const char* sendData, unsigned int sendDataSize,
                              const char* consumerToken,
                              const char* consumerSecret,
                              const char* accessToken, const char* tokenSecret,
                              apr_pool_t* pool) {
  Buf buf;
  Params params;
  int didOne;

  allocBuf(&buf, 64, pool);
  allocParams(&params, 8, pool);

  buildBaseString(&buf, &params, url, method, sendData, sendDataSize,
                  consumerToken, accessToken, pool);
  addParam(&params, "oauth_signature",
           generateHmac(buf.buf, consumerSecret, tokenSecret, pool));

  // Now generate the final header
  buf.len = 0;
  appendStr(&buf, "OAuth ");
  didOne = 0;
  for (unsigned int inc = 0; inc < params.len; inc++) {
    if (!strncmp(params.params[inc].name, "oauth_", 6)) {
      if (didOne) {
        appendChar(&buf, ',');
      } else {
        didOne = 1;
      }
      appendEncoded(&buf, params.params[inc].name);
      appendChar(&buf, '=');
      if (params.params[inc].val != NULL) {
        appendChar(&buf, '"');
        appendEncoded(&buf, params.params[inc].val);
        appendChar(&buf, '"');
      }
    }
  }

  return buf.buf;
}
*/