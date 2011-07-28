#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include <apr_strings.h>
#include <apr_random.h>
#include <apr_time.h>

#include <openssl/bio.h>
#include <openssl/hmac.h>
#include <openssl/sha.h>

#include <apib_common.h>

typedef struct {
  char* buf;
  size_t len;
  size_t size;
  apr_pool_t* pool;
} Buf;

typedef struct {
  char* name;
  char* val;
} Param;

typedef struct {
  Param* params;
  size_t len;
  size_t size;
  apr_pool_t* pool;
} Params;

static void allocBuf(Buf* b, size_t size, apr_pool_t* pool)
{
  b->buf = (char*)apr_palloc(pool, size);
  b->len = 0;
  b->size = size;
  b->pool = pool;
  b->buf[0] = 0;
}

static void allocParams(Params* p, size_t size, apr_pool_t* pool)
{
  p->len = 0;
  p->size = size;
  p->params = (Param*)apr_palloc(pool, sizeof(Param) * p->size);
  p->pool = pool;
}

static void appendStr(Buf* b, const char* str)
{
  size_t strLen = strlen(str);

  if ((b->len + strLen + 1) > b->size) {
    char* oldBuf = b->buf;
    if (strLen > b->size) {
      b->size += (strLen + 1);
    } else {
      b->size *= 2;
    }
    b->buf = apr_palloc(b->pool, b->size);
    memcpy(b->buf, oldBuf, b->len);
  }
  memcpy(b->buf + b->len, str, strLen + 1);
  b->len += strLen;
}

static void appendChar(Buf* b, char ch)
{
  if ((b->len + 2) > b->size) {
    char* oldBuf = b->buf;
    b->size *= 2;
    b->buf = apr_palloc(b->pool, b->size);
    memcpy(b->buf, oldBuf, b->len);
  }
  b->buf[b->len] = ch;
  b->buf[b->len + 1] = 0;
  b->len++;
}

/* Encode a string as described by the OAuth 1.0a spec, specifically
 * RFC5849. */
static void appendEncoded(Buf* b, const char* str)
{
  size_t p = 0;
  char buf[8];

  while (str[p] != 0) {
    if (isalnum(str[p]) ||
	(str[p] == '-') || (str[p] == '.') ||
	(str[p] == '_') || (str[p] == '~')) {
      appendChar(b, str[p]);
      
    } else {
      sprintf(buf, "%%%02X", str[p]);
      appendStr(b, buf);
    }
    p++;
  }
}

/*
 * Decode a string as described by the HTML spec and as commonly implemented. */
static char* decode(const char* str, apr_pool_t* pool)
{
  char* ret = apr_palloc(pool, strlen(str) + 1);
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

static void ensureParamsSize(Params* params)
{
  if (params->len >= params->size) {
    Param* old = params->params;

    params->size *= 2;
    params->params = 
      (Param*)apr_palloc(params->pool, sizeof(Param) * params->size);
    memcpy(params->params, old, sizeof(Param) * params->len);
  }
}

static void readParams(Params* params, char* str, size_t len)
{
  char* tok;
  char* last;

  tok = apr_strtok(str, "&", &last);
  while (tok != NULL) {
    char* param = apr_pstrdup(params->pool, tok);
    char* tok2;
    char* last2;

    ensureParamsSize(params);
    tok2 = apr_strtok(param, "=", &last2);
    params->params[params->len].name = tok2;
    tok2 = apr_strtok(NULL, "&", &last2);
    if (tok2 == NULL) {
      params->params[params->len].val = NULL;
    } else {
      params->params[params->len].val = decode(tok2, params->pool);
    }
    
    params->len++;
      
    tok = apr_strtok(NULL, "&", &last);
  }
}

static void addParam(Params* params, char* name, char* val)
{
  ensureParamsSize(params);
  params->params[params->len].name = name;
  params->params[params->len].val = val;
  params->len++;
}

static int compareParam(const void* v1, const void* v2) 
{
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

static char* generateHmac(const char* base,
			  const char* consumerSecret,
			  const char* tokenSecret,
			  apr_pool_t* pool)
{
  Buf keyBuf;
  unsigned char* hmac = apr_palloc(pool, EVP_MAX_MD_SIZE);
  unsigned int hmacLen;
  BIO* mem;
  BIO* baser;
  char* ret;
  
  allocBuf(&keyBuf, 64, pool);
  appendEncoded(&keyBuf, consumerSecret);
  appendChar(&keyBuf, '&');
  if (tokenSecret != NULL) {
    appendEncoded(&keyBuf, tokenSecret);
  }
  
  HMAC(EVP_sha1(), keyBuf.buf, keyBuf.len, (const unsigned char*)base, 
       strlen(base), hmac, &hmacLen);

  mem = BIO_new(BIO_s_mem());
  baser = BIO_new(BIO_f_base64());
  BIO_push(baser, mem);
  BIO_write(baser, hmac, hmacLen);
  if (BIO_flush(baser) != 1) {
    assert(FALSE);
  }
  hmacLen = BIO_ctrl_pending(mem);
  ret = apr_palloc(pool, hmacLen + 1);
  BIO_read(mem, ret, hmacLen);
  BIO_free_all(baser);
  ret[hmacLen] = 0;

  return ret;
}

static char* makeRandom(apr_pool_t* pool)
{
  unsigned long p1;
  unsigned long p2;

  apr_generate_random_bytes((unsigned char*)&p1, sizeof(long));
  apr_generate_random_bytes((unsigned char*)&p2, sizeof(long));
  return apr_psprintf(pool, "%lx%lx", p1, p2);
}

static void buildBaseString(Buf* buf,
			    Params* params,
			    const apr_uri_t* url,
			    const char* method,
			    const char* sendData,
			    unsigned int sendDataSize,
			    const char* consumerToken,
			    const char* accessToken,
			    apr_pool_t* pool)
{
  Buf paramBuf;
  char* num;
  char* nonce;

  appendStr(buf, method);
  appendChar(buf, '&');

  /* Encoded and normalized URL */
  appendEncoded(buf, url->scheme);
  appendEncoded(buf, "://");
  appendEncoded(buf, url->hostname);
  if (!(((url->port == 80) && !strcmp(url->scheme, "http")) ||
	((url->port == 443) && !strcmp(url->scheme, "https")) ||
	((url->port_str == NULL)))) {
    appendEncoded(buf, ":");
    appendEncoded(buf, url->port_str);
  }
  if (url->path != NULL) {
    appendEncoded(buf, url->path);
  }

  /* Parse query params */
  if (url->query != NULL) {
    readParams(params, apr_pstrdup(pool, url->query), strlen(url->query));
  }

  /* Parse form body */
  if (sendData != NULL) {
    readParams(params, apr_pstrdup(pool, sendData), sendDataSize);
  }

  /* Add additional OAuth params */
  if (consumerToken != NULL) {
    addParam(params, "oauth_consumer_key", apr_pstrdup(pool, consumerToken));
  }
  if (accessToken != NULL) {
    addParam(params, "oauth_token", apr_pstrdup(pool, accessToken));
  }
  addParam(params, "oauth_version", "1.0");
  addParam(params, "oauth_signature_method", "HMAC-SHA1");
  num = apr_psprintf(pool, "%lli", apr_time_sec(apr_time_now()));
  addParam(params, "oauth_timestamp", num);
  nonce = makeRandom(pool);
  addParam(params, "oauth_nonce", nonce);

  /* Normalize and output params */
  qsort(params->params, params->len, sizeof(Param), compareParam);

  allocBuf(&paramBuf, 64, pool);
  for (unsigned int inc = 0; inc < params->len; inc++) {
    if (inc > 0) {
      appendChar(&paramBuf, '&');
    }
    appendEncoded(&paramBuf, params->params[inc].name);
    appendChar(&paramBuf, '=');
    if (params->params[inc].val != NULL) {
      appendEncoded(&paramBuf, params->params[inc].val);
    }
  }

  /* Attach them, which encodes them again */
  appendChar(buf, '&');
  appendEncoded(buf, paramBuf.buf);
}

char* oauth_MakeQueryString(const apr_uri_t* url,
			    const char* method,
			    const char* sendData,
			    unsigned int sendDataSize,
			    const char* consumerToken,
			    const char* consumerSecret,
			    const char* accessToken,
			    const char* tokenSecret,
			    apr_pool_t* pool)
{
  Buf buf;
  Params params;
  int didOne;

  allocBuf(&buf, 64, pool);
  allocParams(&params, 8, pool);

  buildBaseString(&buf, &params, url, method, sendData, sendDataSize,
		  consumerToken, accessToken, pool);
  addParam(&params, "oauth_signature", 
	   generateHmac(buf.buf, consumerSecret, tokenSecret, pool));

  /* Now generate the final query string */
  buf.len = 0;
  didOne = 0;
  for (unsigned int inc = 0; inc < params.len; inc++) {
    if (didOne) {
      appendChar(&buf, '&');
    } else {
      didOne = 1;
    }
    appendEncoded(&buf, params.params[inc].name);
    appendChar(&buf, '=');
    if (params.params[inc].val != NULL) {
      appendEncoded(&buf, params.params[inc].val);
    }
  }
  
  return buf.buf; 
}

char* oauth_MakeAuthorization(const apr_uri_t* url,
			      const char* method,
			      const char* sendData,
			      unsigned int sendDataSize,
			      const char* consumerToken,
			      const char* consumerSecret,
			      const char* accessToken,
			      const char* tokenSecret,
			      apr_pool_t* pool)
{
  Buf buf;
  Params params;
  int didOne;

  allocBuf(&buf, 64, pool);
  allocParams(&params, 8, pool);

  buildBaseString(&buf, &params, url, method, sendData, sendDataSize,
		  consumerToken, accessToken, pool);
  addParam(&params, "oauth_signature", 
	   generateHmac(buf.buf, consumerSecret, tokenSecret, pool));

  /* Now generate the final header */
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
