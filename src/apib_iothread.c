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

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <stdarg.h>

#include <apib.h>

#include <apr_lib.h>
#include <apr_network_io.h>
#include <apr_poll.h>
#include <apr_pools.h>
#include <apr_portable.h>
#include <apr_signal.h>
#include <apr_strings.h>
#include <apr_time.h>

#include <openssl/err.h>
#include <openssl/ssl.h>
#include <openssl/evp.h>

#define DEBUG 0

#define SEND_BUF_SIZE     65536

#define MAX_POLL_TIME     apr_time_from_sec(2)

#define STATE_NONE          0
#define STATE_CONNECTING    1
#define STATE_SSL_HANDSHAKE 2
#define STATE_SENDING       3
#define STATE_RECV_READY    4
#define STATE_RECV_START    5
#define STATE_RECV_HDRS     6
#define STATE_RECV_BODY     7
#define STATE_RECV_BODY_CHNK 8
#define STATE_RECV_TRAILERS 9
#define STATE_SEND_READY    10
#define STATE_CLOSING       11
#define STATE_FAILED        12
#define STATE_PANIC         99

#define STATUS_WANT_READ  1
#define STATUS_WANT_WRITE 2
#define STATUS_WANT_YIELD 3
#define STATUS_CONTINUE   0

typedef struct {
  int             index;
  IOArgs*         ioArgs;
  const URLInfo*  url;
  int             pollIndex;
  apr_pool_t*     transPool;
  apr_pool_t*     connPool;
  RandState       random;
  apr_socket_t*   sock;
  SSL*            ssl;
  int             state;
  int             delayMillis;
  int             bufPos;
  int             bufLen;
  int             sendBufPos;
  char*           buf;
  int             httpStatus;
  int             chunked;
  int             chunkLen;
  int             contentLength;
  int             closeRequested;
  int             contentRead;
  apr_time_t      requestStart;
  apr_status_t    panicStatus;
  unsigned long   wakeups;
  LineState       line;
} ConnectionInfo;

#if DEBUG
#define SETSTATE(c, s) \
  printf("%lx %i -> %i (line %i)\n", (unsigned long)c, c->state, s, __LINE__);	\
  c->state = s;
#define MASKSTATE(c, s) \
  printf("%lx MASK %i -> %i (line %i)\n", (unsigned long)c, c->state, s, __LINE__); \
  c->state |= s;
#define UNMASKSTATE(c, s) \
  printf("%lx UNMASK %i -> %i (line %i)\n", (unsigned long)c, c->state, s, __LINE__); \
  c->state &= ~s;
#else
#define SETSTATE(c, s) \
  c->state = s;
#define MASKSTATE(c, s) \
  c->state |= s;
#define UNMASKSTATE(c, s) \
  c->state &= ~s;
#endif

static char* trimString(char* s)
{
  char* ret = s;
  unsigned int len;

  while (apr_isspace(*ret)) {
    ret++;
  }

  len = strlen(s);
  while (len > 0 && apr_isspace(ret[len - 1])) {
    ret[len - 1] = 0;
    len--;
  }

  return ret;
}

static int keepAlive(const ConnectionInfo* conn)
{
  return (KeepAlive != KEEP_ALIVE_NEVER);
}

static int recycle(ConnectionInfo* conn)
{
  if (conn->ioArgs->thinkTime > 0) {
    conn->delayMillis = conn->ioArgs->thinkTime;
    return STATUS_WANT_YIELD;
  } 
  return STATUS_CONTINUE;
}

static void makeConnection(ConnectionInfo* conn, apr_pollfd_t* fd,
			   apr_sockaddr_t* myAddr,      
                           apr_pool_t* p)
{
  apr_status_t s;
  apr_socket_t* sock;

  apr_pool_clear(conn->connPool);
  apr_pool_create(&(conn->transPool), conn->connPool);
  s = apr_socket_create(&sock, APR_INET, SOCK_STREAM, APR_PROTO_TCP, conn->connPool);
  if (s != APR_SUCCESS) {
    goto panic;
  }
  s = apr_socket_opt_set(sock, APR_SO_REUSEADDR, 1);
  if (s != APR_SUCCESS) {
    goto panic;
  }
  s = apr_socket_opt_set(sock, APR_SO_LINGER, 0);
  if (s != APR_SUCCESS) {
    goto panic;
  }

  s = apr_socket_opt_set(sock, APR_SO_NONBLOCK, 1);
  if (s != APR_SUCCESS) {
    goto panic;
  }

  s = apr_socket_opt_set(sock, APR_TCP_NODELAY, 1);
  if (s != APR_SUCCESS) {
    goto panic;
  }

  if (conn->url->isSsl) {
    int fd;

    conn->ssl = SSL_new(conn->ioArgs->sslCtx);
    apr_os_sock_get(&fd, sock);
    SSL_set_fd(conn->ssl, fd);
    SSL_set_connect_state(conn->ssl);
  }

  conn->sock = sock;
  fd->desc.s = sock;

  RecordConnectionOpen();

  SETSTATE(conn, STATE_CONNECTING);
  return;

panic:
  conn->panicStatus = s;
  SETSTATE(conn, STATE_PANIC);
}

static int setupConnection(ConnectionInfo* conn)
{
  apr_status_t s;


  s = apr_socket_connect(conn->sock, url_GetAddress(conn->url, conn->index));

  if (s == APR_EINPROGRESS) {
    return STATUS_WANT_WRITE;
  } else if (s != APR_SUCCESS) {
    /* Probably all the fds are busy */
    SETSTATE(conn, STATE_FAILED);
    return STATUS_CONTINUE;
  }
  
  if (conn->url->isSsl) {
    SETSTATE(conn, STATE_SSL_HANDSHAKE);
  } else {
    SETSTATE(conn, STATE_SEND_READY);
  }

  return STATUS_CONTINUE;
}

static char* key_info(EVP_PKEY *key, int is_dh)
{
  char *ret;
  int len;
  char *fmt;
  switch (EVP_PKEY_id(key)) {
    case EVP_PKEY_RSA:
      fmt = "RSA-%ibits";
      len = snprintf(NULL, 0, fmt, EVP_PKEY_bits(key));
      ret = malloc(len+1);
      sprintf(ret, fmt, EVP_PKEY_bits(key));
      break;
    case EVP_PKEY_DH:
      if (is_dh)
        fmt = "DHE-%ibits";
      else
        fmt = "DH-%ibits";
      len = snprintf(NULL, 0, fmt, EVP_PKEY_bits(key));
      ret = malloc(len+1);
      sprintf(ret, fmt, EVP_PKEY_bits(key));
      break;
    case EVP_PKEY_EC:
      if (is_dh)
        fmt = "ECDHE-%s";
      else
        fmt = "ECDSA-%s";
      EC_KEY *ec = EVP_PKEY_get1_EC_KEY(key);
      int nid;
      const char *cname;
      nid = EC_GROUP_get_curve_name(EC_KEY_get0_group(ec));
      EC_KEY_free(ec);
      cname = EC_curve_nid2nist(nid);
      if (cname == NULL) {
        cname = OBJ_nid2sn(nid);
      }
      len = snprintf(NULL, 0, fmt, cname);
      ret = malloc(len+1);
      sprintf(ret, fmt, cname);
      break;
    case NID_X25519:
    case NID_X448:
    case NID_ED25519:
    case NID_ED448:
      ret = strdup(OBJ_nid2sn(EVP_PKEY_id(key)));
      break;
    default:
      fmt = "%s-%ibits";
      const char *name = OBJ_nid2sn(EVP_PKEY_id(key));
      int bits = EVP_PKEY_bits(key);
      len = snprintf(NULL, 0, fmt, name, bits);
      ret = malloc(len+1);
      sprintf(ret, fmt, name, bits);
      break;
  }
  return ret;
}

static char* tmpKey(SSL *ssl)
{
  char *ret;
  EVP_PKEY *key;
  if (!SSL_get_server_tmp_key(ssl, &key)) {
    return strdup("");
  }
  ret = key_info(key, 1);
  EVP_PKEY_free(key);
  return ret;
}

static const char *get_sigtype(int nid)
{
  switch (nid) {
  case EVP_PKEY_RSA:
    return "RSA";

  case EVP_PKEY_RSA_PSS:
    return "RSA-PSS";

  case EVP_PKEY_DSA:
    return "DSA";

  case EVP_PKEY_EC:
    return "ECDSA";

  case NID_ED25519:
    return "Ed25519";

  case NID_ED448:
    return "Ed448";

  case NID_id_GostR3410_2001:
    return "gost2001";

  case NID_id_GostR3410_2012_256:
    return "gost2012_256";

  case NID_id_GostR3410_2012_512:
    return "gost2012_512";

  default:
    return OBJ_nid2sn(nid);
  }
}

static char* srvCert(SSL* ssl)
{
  char *ret;
  X509 *cert;
  cert = SSL_get_peer_certificate(ssl);
  char* pkey_type;
  const char* hash_name = NULL;
  const char* sig_alg_name = NULL;
  const char* tmp;

  if (cert) {
    EVP_PKEY *pkey;
    pkey = X509_get0_pubkey(cert);
    pkey_type = key_info(pkey, 0);
    int nid;
    if (SSL_get_peer_signature_nid(ssl, &nid))
        hash_name = OBJ_nid2sn(nid);
    if (SSL_get_peer_signature_type_nid(ssl, &nid))
        sig_alg_name = get_sigtype(nid);

    char *fmt;
    if (hash_name && sig_alg_name) {
        fmt = "%s,%s-%s";
    }
    else if (hash_name || sig_alg_name) {
        fmt = "%s,%s";
        if (hash_name)
          tmp = hash_name;
        else
          tmp = sig_alg_name;
    } else {
        fmt = "%s";
    }

    int len;
    if (hash_name && sig_alg_name) {
      len = snprintf(NULL, 0, fmt, pkey_type, sig_alg_name, hash_name);
      ret = malloc(len + 1);
      sprintf(ret, fmt, pkey_type, sig_alg_name, hash_name);
      free(pkey_type);
    } else if (hash_name || sig_alg_name) {
      len = snprintf(NULL, 0, fmt, pkey_type, tmp);
      ret = malloc(len + 1);
      sprintf(ret, fmt, pkey_type, tmp);
      free(pkey_type);
    } else {
      ret = pkey_type;
    }

    X509_free(cert);
  }
  return ret;
}

static int handshakeSsl(ConnectionInfo* conn)
{
  int sslStatus;
  int sslErr;

  sslStatus = SSL_do_handshake(conn->ssl); 
  if (sslStatus <= 0) {
    sslErr = SSL_get_error(conn->ssl, sslStatus);
    switch(sslErr) {
    case SSL_ERROR_WANT_READ:
      return STATUS_WANT_READ;
    case SSL_ERROR_WANT_WRITE:
      return STATUS_WANT_WRITE;
    default:
      RecordSocketError();
      SETSTATE(conn, STATE_FAILED);
      return STATUS_CONTINUE;
    }
    assert(FALSE);
  }

  if (!conn->ioArgs->tlsConfig) {
    int len;
    char *fmt = "%s,%s,%s,%s\n";
    const char *name = SSL_get_cipher_name(conn->ssl);
    const char *vers = SSL_get_version(conn->ssl);
    char *eph_key = tmpKey(conn->ssl);
    char *cer_key = srvCert(conn->ssl);
    len = snprintf(NULL, 0, fmt, vers, name, eph_key, cer_key);
    char *ret = malloc(len+1);
    if (!ret) {
      SETSTATE(conn, STATE_FAILED);
      return STATUS_CONTINUE;
    }
    if (len != sprintf(ret, fmt, vers, name, eph_key, cer_key)) {
      SETSTATE(conn, STATE_FAILED);
      return STATUS_CONTINUE;
    }
    free(eph_key);
    free(cer_key);
    conn->ioArgs->tlsConfig = ret;
  }

  SETSTATE(conn, STATE_SEND_READY);
  return STATUS_CONTINUE;
}

static void cleanupConnection(ConnectionInfo* conn)
{
  if ((conn->url->isSsl) && (conn->ssl != NULL)) {
    SSL_shutdown(conn->ssl);
    SSL_free(conn->ssl);
    conn->ssl = NULL;
  }
  apr_socket_close(conn->sock);
}

static void appendRequestLine(ConnectionInfo* conn, 
                              const char* fmt, ...)
{
  va_list args;

  va_start(args, fmt);
  conn->bufPos +=
    apr_vsnprintf(conn->buf + conn->bufPos, conn->bufLen - conn->bufPos,
                  fmt, args);
  va_end(args);
}

static void buildRequest(ConnectionInfo* conn)
{
  char* path;
  char* query = NULL;

  apr_pool_clear(conn->transPool);

  conn->bufPos = 0;
  conn->bufLen = SEND_BUF_SIZE;
  conn->sendBufPos = 0;

  if (conn->url->url.path == NULL) {
    path = "/";
  } else {
    path = conn->url->url.path;
  }

  if ((OAuthCK != NULL) && (OAuthCS != NULL)) {
    query = oauth_MakeQueryString(&(conn->url->url), 
				  conn->ioArgs->httpVerb,
				  NULL, 0, 
				  OAuthCK, OAuthCS,
				  OAuthAT, OAuthAS,
				  conn->transPool);
  } else {
    query = conn->url->url.query;
  }

  if (query != NULL) {
    appendRequestLine(conn, "%s %s?%s HTTP/1.1\r\n", 
                      conn->ioArgs->httpVerb, path, query);
  } else {
    appendRequestLine(conn, "%s %s HTTP/1.1\r\n", 
                      conn->ioArgs->httpVerb, path);
  }

  appendRequestLine(conn, "User-Agent: %s\r\n", USER_AGENT);

  if (!conn->ioArgs->hostHeaderOverride) {
    if (conn->url->url.port_str != NULL) {
      appendRequestLine(conn, "Host: %s:%s\r\n", conn->url->url.hostname,
			conn->url->url.port_str);
    } else {
      appendRequestLine(conn, "Host: %s\r\n", conn->url->url.hostname);
    }
  }

  if (conn->ioArgs->sendDataSize > 0) {
    char* cType = conn->ioArgs->contentType;
    appendRequestLine(conn, "Content-Length: %i\r\n", 
                      conn->ioArgs->sendDataSize);
    if (cType == NULL) {
      cType = DEFAULT_CONTENT_TYPE;
    }
    appendRequestLine(conn, "Content-Type: %s\r\n", cType);
  }
  if (!keepAlive(conn)) {
     appendRequestLine(conn, "Connection: close\r\n");
  }
  /* We are doing OAuth on the query line for now -- restore this later
   * and possibly make it an option. 
  if ((OAuthCK != NULL) && (OAuthCS != NULL)) {
    appendRequestLine(conn, 
		   "Authorization: %s\r\n",
		   oauth_MakeAuthorization(conn->ioArgs->url, 
					   conn->ioArgs->httpVerb,
					   NULL, 0, 
					   OAuthCK, OAuthCS,
					   OAuthAT, OAuthAS,
					   conn->transPool));
  }
  */

  for (unsigned int i = 0; i < conn->ioArgs->numHeaders; i++) {
    appendRequestLine(conn, "%s\r\n", conn->ioArgs->headers[i]);
  }

  appendRequestLine(conn, "\r\n");

  assert(conn->bufPos <= conn->bufLen);

  conn->bufLen = conn->bufPos;
  conn->bufPos = 0;
  SETSTATE(conn, STATE_SENDING);

  if (conn->ioArgs->verbose) {
    printf("%s", conn->buf);
  }
}

static int writeRequestSsl(ConnectionInfo* conn)
{
  int sslStatus;
  int sslErr;
#if DEBUG
  char buf[128];
#endif

  if (conn->bufLen > conn->bufPos) {
#if DEBUG
    printf("Writing %i bytes to SSL\n", conn->bufLen - conn->bufPos);
#endif
    sslStatus = SSL_write(conn->ssl, conn->buf + conn->bufPos,
			  conn->bufLen - conn->bufPos);
  } else {
#if DEBUG
    printf("Writing %i bytes to SSL\n", conn->ioArgs->sendDataSize - conn->sendBufPos);
#endif
    sslStatus = SSL_write(conn->ssl, conn->ioArgs->sendData + conn->sendBufPos,
			  conn->ioArgs->sendDataSize - conn->sendBufPos);
  }
  conn->ioArgs->writeCount++;
  conn->ioArgs->writeBytes += sslStatus;
  
#if DEBUG
  printf("SSL return %i\n", sslStatus);
#endif

  if (sslStatus <= 0) {
    sslErr = SSL_get_error(conn->ssl, sslStatus);
    switch (sslErr) {
    case SSL_ERROR_WANT_WRITE:
      /* Equivalent to EAGAIN */
      return STATUS_WANT_WRITE;
    case SSL_ERROR_WANT_READ:
      return STATUS_WANT_READ;
    default:
#if DEBUG 
      fprintf(stderr, "SSL write error: %i\n", sslErr);
      ERR_error_string_n(ERR_get_error(), buf, 128);
      fprintf(stderr, "  SSL error: %s\n", buf);
#endif
      SETSTATE(conn, STATE_FAILED);
      RecordSocketError();
      return STATUS_CONTINUE;
    }
  }

  if (conn->bufLen > conn->bufPos) {
    conn->bufPos += sslStatus;
  } else {
    conn->sendBufPos += sslStatus;
  }
  if ((conn->bufPos >= conn->bufLen) &&
      (conn->sendBufPos >= conn->ioArgs->sendDataSize)) {
    /* Did all the writing -- ready to receive */
    SETSTATE(conn, STATE_RECV_READY);
  }
  /* Since OpenSSL virtualizes all I/O, we don't really know whether to
   * wait for read or write yet, so always continue here and try SSL_read */
  return STATUS_CONTINUE;
}

static int writeRequestNonSsl(ConnectionInfo* conn)
{
  apr_status_t s;
  apr_size_t written;
  struct iovec bufs[2];

  /* Buffer 0 is always the header, 1 is always the body */
  /* We just set the sizes to zero when they're not relevant... */

  bufs[0].iov_base = conn->buf + conn->bufPos;
  bufs[0].iov_len = conn->bufLen - conn->bufPos;
  bufs[1].iov_base = conn->ioArgs->sendData + conn->sendBufPos;
  bufs[1].iov_len = conn->ioArgs->sendDataSize - conn->sendBufPos;

  s = apr_socket_sendv(conn->sock, bufs, 2, &written);
  conn->ioArgs->writeCount++;
  conn->ioArgs->writeBytes += written;
  if (APR_STATUS_IS_EAGAIN(s)) {
    return STATUS_WANT_WRITE;
  }

#if DEBUG
  printf("Headers: %i out of %i Body: %i out of %i sent %zu\n",
	 conn->bufPos, conn->bufLen, 
	 conn->sendBufPos, conn->ioArgs->sendDataSize, written);
#endif

  /* Update positions even if we got an error since that's the spec */
  if (conn->bufPos < conn->bufLen) {
    if (written > (conn->bufLen - conn->bufPos)) {
      written -= (conn->bufLen - conn->bufPos);
      conn->bufPos = conn->bufLen;
    } else {
      conn->bufPos += written;
      written = 0;
    }
  }
  if (written > 0) {
    conn->sendBufPos += written;
  }

  if (s != APR_SUCCESS) {
#if DEBUG
    char buf[128];
    apr_strerror(s, buf, 128);
    fprintf(stderr, "Send error: %s\n", buf);
#endif
    SETSTATE(conn, STATE_FAILED);
    RecordSocketError();
    return STATUS_CONTINUE;
  } else if ((conn->bufPos >= conn->bufLen) &&
	     (conn->sendBufPos >= conn->ioArgs->sendDataSize)) {
    /* Did all the writing -- ready to receive */
    SETSTATE(conn, STATE_RECV_READY);
    /* Explicitly give up the thread here in order to let another request
     * run in the event that we have a response right away. */
    return STATUS_WANT_READ;
  }
  return STATUS_CONTINUE;
}

static int writeRequest(ConnectionInfo* conn)
{
  if (conn->url->isSsl) {
    return writeRequestSsl(conn);
  } else {
    return writeRequestNonSsl(conn);
  }
}

static void startReadRequest(ConnectionInfo* conn) 
{
  SETSTATE(conn, STATE_RECV_START);
  linep_Start(&(conn->line), conn->buf, SEND_BUF_SIZE, 0);
  linep_SetHttpMode(&(conn->line), 1);
}

static void processRequestLine(ConnectionInfo* conn)
{
  char* tok;
  
  if (conn->ioArgs->verbose) {
    printf("%s\n", linep_GetLine(&(conn->line)));
  }

  /* Should get "HTTP/1.1". Check later? */
  linep_NextToken(&(conn->line), " ");
  tok = linep_NextToken(&(conn->line), " ");
  if (tok == NULL) {
    /* Something very weird */
    conn->httpStatus = 999;
  } else {
    conn->httpStatus = atoi(tok);
  }

  /* Initialize other parameters */
  conn->contentLength = -1;
  conn->chunked = 0;
  conn->chunkLen = 1;
  conn->closeRequested = 0;
}

static void processHeader(ConnectionInfo* conn)
{
  char* name;
  char* value;

  name = linep_NextToken(&(conn->line), " :");
  value = linep_NextToken(&(conn->line), "\r\n");

  if (value == NULL) {
    /* Another weird response so don't go on */
    return;
  }

  value = trimString(value);

  if (conn->ioArgs->verbose) {
    printf("\"%s\" : \"%s\"\n", name, value);
  }

  if (!strcasecmp(name, "Content-Length")) {
    conn->contentLength = atoi(value);
  } else if (!strcasecmp(name, "Connection")) {
    if (!strcasecmp(value, "close")) {
      conn->closeRequested = 1;
    }
  } else if (!strcasecmp(name, "Transfer-Encoding") && !strcasecmp(value, "chunked")) {
    conn->chunked = 1;
  }
}

static int requestComplete(ConnectionInfo* conn)
{
  const URLInfo* lastUrl;

  RecordResult(conn->ioArgs, conn->httpStatus, 
	       apr_time_now() - conn->requestStart);
  conn->requestStart = 0LL;
  if (JustOnce) {
    conn->ioArgs->keepRunning = FALSE;
  }
  if ((conn->closeRequested) ||
      !keepAlive(conn) ||
      (conn->contentLength < 0)) {
    /* No keep-alive or server closed -- set a state so it happens */
    SETSTATE(conn, STATE_CLOSING);
    return STATUS_CONTINUE;
  } 

  /* Also have to close if we are testing a lot of servers */
  lastUrl = conn->url;
  conn->url = url_GetNext(conn->random);
  if (!url_IsSameServer(conn->url, lastUrl, conn->index)) {
    SETSTATE(conn, STATE_CLOSING);
    return STATUS_CONTINUE;
  }    

  /* Re-use the connection for the next request */
  SETSTATE(conn, STATE_SEND_READY);
  return recycle(conn);
}

static int readRequest(ConnectionInfo* conn)
{
  apr_size_t readLen;
  char* readBuf;
#if DEBUG
  char buf[128];
#endif

  linep_GetReadInfo(&(conn->line), &readBuf, &readLen);

  if (conn->url->isSsl) {
    int sslStatus;
    int sslErr;

    sslStatus = SSL_read(conn->ssl, readBuf, readLen);
#if DEBUG
    printf("SSL_read = %i err = %i\n", sslStatus,
	   SSL_get_error(conn->ssl, sslStatus));
#endif
    conn->ioArgs->readCount++;
    if (sslStatus <= 0) {
      sslErr = SSL_get_error(conn->ssl, sslStatus);
      switch (sslErr) {
      case SSL_ERROR_WANT_READ:
	return STATUS_WANT_READ;
      case SSL_ERROR_WANT_WRITE:
	return STATUS_WANT_WRITE;
      case SSL_ERROR_ZERO_RETURN:
        if (conn->closeRequested) {
          return requestComplete(conn);
        } else {
          SETSTATE(conn, STATE_FAILED);
          RecordSocketError();
        }
        return STATUS_CONTINUE;
      default:
#if DEBUG
	fprintf(stderr, "SSL receive error: %i\n", sslErr);
	ERR_error_string_n(ERR_get_error(), buf, 128);
	fprintf(stderr, "  SSL error: %s\n", buf);
#endif
	SETSTATE(conn, STATE_FAILED);
	RecordSocketError();
	return STATUS_CONTINUE;
      }
      assert(FALSE);
    }

    readLen = sslStatus;

  } else {
    apr_status_t s;

    s = apr_socket_recv(conn->sock, readBuf, &readLen);
    conn->ioArgs->readCount++;
    if (APR_STATUS_IS_EAGAIN(s)) {
      return STATUS_WANT_READ;
    }
    if (APR_STATUS_IS_EOF(s) && conn->closeRequested) {
      return requestComplete(conn);
    }

    if (s != APR_SUCCESS) {
#if DEBUG
      apr_strerror(s, buf, 128);
      fprintf(stderr, "Receive error: %s\n", buf);
#endif
      SETSTATE(conn, STATE_FAILED);
      RecordSocketError();
      return STATUS_CONTINUE;
    }
  }

  conn->ioArgs->readBytes += readLen;
  linep_SetReadLength(&(conn->line), readLen);

  while (((conn->state == STATE_RECV_START) || conn->state == STATE_RECV_HDRS) &&
	 linep_NextLine(&(conn->line))) {
    char* line = linep_GetLine(&(conn->line));
    if (conn->state == STATE_RECV_START) {
      processRequestLine(conn);
      SETSTATE(conn, STATE_RECV_HDRS);
    } else if (!strcmp(line, "")) {
      /* Blank line -- end of headers */
      if (conn->chunked) {
	SETSTATE(conn, STATE_RECV_BODY_CHNK);
      } else {
	SETSTATE(conn, STATE_RECV_BODY);
      }
      conn->contentRead = 0;
      break;
    } else {
      processHeader(conn);
    }
  }

  if ((conn->state != STATE_RECV_BODY) &&
      (conn->state != STATE_RECV_BODY_CHNK) &&
      (conn->state != STATE_RECV_TRAILERS)) {
    /* There are more lines to read and we haven't found a complete one yet */
    if (linep_Reset(&(conn->line))) {
      /* Line is too long for our buffer */
      SETSTATE(conn, STATE_PANIC);
    }
    return STATUS_CONTINUE;
  }

  while (TRUE) {
    if (conn->state == STATE_RECV_BODY_CHNK) {
      while (conn->chunkLen > 0) {
	if (linep_NextLine(&(conn->line))) {
	  conn->chunkLen--;
	} else {
	  /* We didn't read enough full chunk lines -- try again later */
	  if (linep_Reset(&(conn->line))) {
	    /* Line is too long for our buffer */
	    SETSTATE(conn, STATE_PANIC);
	  }
	  return STATUS_CONTINUE;
	}
      }

      char* lenStr = linep_NextToken(&(conn->line), ";\r\n");
      long len = strtol(lenStr, NULL, 16);
#if DEBUG
      char* line = linep_GetLine(&(conn->line));
      printf("Read chunk line \"%s\" len=%li\n", line, len);
#endif
      if (len == 0) {
	SETSTATE(conn, STATE_RECV_TRAILERS);
      } else {
	if (conn->ioArgs->verbose) {
	  printf("  Next chunk is %li bytes long\n", len);
	}
	conn->contentLength = len;
	SETSTATE(conn, STATE_RECV_BODY);
      }
    }

    if (conn->state == STATE_RECV_TRAILERS) {
#if DEBUG
      printf("Trailers remaining:");
      linep_Debug(&(conn->line), stdout);
      printf("\n");
#endif
      while (linep_NextLine(&(conn->line))) {
	char* line = linep_GetLine(&(conn->line));
#if DEBUG
	printf("Read trailer line \"%s\"\n", line);
#endif
	if (!strcmp(line, "")) {
	  /* Done reading -- back to sending */
	  return requestComplete(conn);
	}
      }
#if DEBUG
      printf("Trailers remaining:");
      linep_Debug(&(conn->line), stdout);
      printf("\n");
#endif
      /* If we get here we didn't read all the trailers */
      if (linep_Reset(&(conn->line))) {
	/* Line is too long for our buffer */
	SETSTATE(conn, STATE_PANIC);
      }
      return STATUS_CONTINUE;
    }

    if (conn->state == STATE_RECV_BODY) {
      linep_GetDataRemaining(&(conn->line), &readLen);
#if DEBUG
      printf("Read %zu body bytes out of %i\n", readLen,
	     conn->contentLength);
#endif
      if (conn->contentLength < 0) {
	conn->closeRequested = 1;
      }
      if ((conn->contentLength > 0) && 
          ((conn->contentLength - conn->contentRead) < readLen)) {
	readLen = conn->contentLength - conn->contentRead;
      }

      conn->contentRead += readLen;
      if (conn->ioArgs->verbose) {
	linep_WriteRemaining(&(conn->line), stdout);
      }
     
      if (conn->contentLength < 0) {
	/* No content-length header -- read until EOF */
	linep_Start(&(conn->line), conn->buf, SEND_BUF_SIZE, 0);
	linep_SetHttpMode(&(conn->line), 1);
	return STATUS_CONTINUE;

      } else if (conn->contentRead >= conn->contentLength) {
	if (conn->chunked) {
	  /* Skip the data we just read */
	  linep_Skip(&(conn->line), readLen);
	  /* Flag that we have to read two complete lines to get chunk len */
	  conn->chunkLen = 2;
	  SETSTATE(conn, STATE_RECV_BODY_CHNK);
	} else {
	  /* Done reading -- can go back to sending! */
	  return requestComplete(conn);
	}
      } else {
        /* Need to read more from the body, so reset the line buffer */
	linep_Start(&(conn->line), conn->buf, SEND_BUF_SIZE, 0);
	linep_SetHttpMode(&(conn->line), 1);
	return STATUS_CONTINUE;
      }
    }
  }
}

static void startTiming(ConnectionInfo* conn)
{
  conn->requestStart = apr_time_now();
}

static int processConnection(ConnectionInfo* conn, apr_pollfd_t* poll, 
                             apr_sockaddr_t* myAddr,
                             apr_pool_t* p)
{
  char errBuf[128];

  if (poll->rtnevents & (APR_POLLERR | APR_POLLHUP)) {
#if DEBUG
    printf("Connection error from poll\n");
#endif
    cleanupConnection(conn);
    makeConnection(conn, poll, myAddr, p);
  }

  while (TRUE) {
    int s = STATUS_CONTINUE;

#if DEBUG
    printf("  state %i\n", conn->state);
#endif

    switch (conn->state) {
    case STATE_PANIC:
      apr_strerror(conn->panicStatus, errBuf, 128);
      fprintf(stderr, "PANIC: Unrecoverable error %i (%s). Thread exiting.\n",
	      conn->panicStatus, errBuf);
      return -1;
      
    case STATE_NONE:
      startTiming(conn);
      makeConnection(conn, poll, myAddr, p);
      break; 

    case STATE_FAILED: 
    case STATE_CLOSING:
      cleanupConnection(conn);
      SETSTATE(conn, STATE_NONE);
      s = recycle(conn);
      break;
      
    case STATE_CONNECTING:
      s = setupConnection(conn);
      break;

    case STATE_SSL_HANDSHAKE:
      s = handshakeSsl(conn);
      break;

    case STATE_SEND_READY:
      if (conn->requestStart == 0LL) {
        startTiming(conn);
      }
      buildRequest(conn);
      break;

    case STATE_SENDING:
      s = writeRequest(conn);
      break;

    case STATE_RECV_READY:
      startReadRequest(conn);
      break;

    case STATE_RECV_START:
    case STATE_RECV_HDRS:
    case STATE_RECV_BODY:
    case STATE_RECV_BODY_CHNK:
    case STATE_RECV_TRAILERS:
      s = readRequest(conn);
      break;
    
    default:
      fprintf(stderr, "Internal state error on poll result: %i\n", conn->state);
      cleanupConnection(conn);
      SETSTATE(conn, STATE_PANIC);
      break;
    }
    
    switch (s) {
    case STATUS_WANT_READ:
      return APR_POLLIN;
    case STATUS_WANT_WRITE:
      return APR_POLLOUT;
    case STATUS_WANT_YIELD:
      return 0;
    default:
      /* Continue looping */
      break;
    }
  }
}

void RunIO(IOArgs* args)
{
  apr_pool_t*       memPool;
  apr_sockaddr_t*   myAddr;
  apr_pollset_t*    pollSet;
  apr_status_t      s;
  char              errBuf[128];
  ConnectionInfo*   conns;
  apr_pollfd_t*     polls;
  int               i;
  int               ps;
  int               pollSize;
  int               pollFlags;
  const apr_pollfd_t*     pollResult;
  
  apr_signal_block(SIGPIPE);

  memPool = args->pool;

  args->readCount = args->writeCount = 0;

  s = apr_sockaddr_info_get(&myAddr, NULL, APR_INET, 0, 0, memPool);
  if (s != APR_SUCCESS) {
    fprintf(stderr, "Error constructing local address!\n");
    goto done;
  }

#ifdef APR_POLLSET_NOCOPY
  pollFlags = APR_POLLSET_NOCOPY;
#else
  pollFlags = 0;
#endif
  s = apr_pollset_create(&pollSet, args->numConnections, memPool, pollFlags); 
  if (s != APR_SUCCESS) {
    apr_strerror(s, errBuf, 80);
    fprintf(stderr, "Error creating pollset: %s\n", errBuf);
    goto done;
  }
  /*if (args->verbose) {
    printf("Created a pollset of type %s\n", apr_pollset_method_name(pollSet));
  } */

  conns = (ConnectionInfo*)apr_palloc(memPool, sizeof(ConnectionInfo) * args->numConnections);
  polls = (apr_pollfd_t*)apr_palloc(memPool, sizeof(apr_pollfd_t) * args->numConnections);

  for (i = 0; i < args->numConnections; i++) {
    conns[i].index = i;
    conns[i].ioArgs = args;
    conns[i].pollIndex = i;
    apr_pool_create(&(conns[i].connPool), memPool);
    apr_pool_create(&(conns[i].transPool), conns[i].connPool);
    conns[i].buf = (char*)apr_palloc(memPool, SEND_BUF_SIZE);
    conns[i].state = STATE_NONE;
    conns[i].wakeups = 0;
    conns[i].delayMillis = 0;
    conns[i].random = (RandState)apr_palloc(memPool, sizeof(RandData));
    conns[i].url = url_GetNext(conns[i].random);

    polls[i].p = memPool;
    polls[i].desc_type = APR_POLL_SOCKET;
    polls[i].reqevents = polls[i].rtnevents = 0;
    polls[i].client_data = &(conns[i]);

    url_InitRandom(conns[i].random);

    ps = processConnection(&(conns[i]), &(polls[i]), myAddr, memPool);
#if DEBUG
    printf("processConnection returned %i\n", ps);
#endif
    if (ps >= 0) {
	polls[i].reqevents = ps | (APR_POLLHUP | APR_POLLERR);
	apr_pollset_add(pollSet, &(polls[i]));
    } else {
      goto done;
    }
  }

  /* This is the main loop... */
  while (Running || (args->keepRunning)) {
    /* Set a maximum poll time -- some times a connection times out and
       the benchmark suite never completes. */
    apr_interval_time_t waitTime = MAX_POLL_TIME;
    apr_time_t now;
    long long firstItem;

    /* Determine how long to wait, based on any requested delays */
    firstItem = pq_PeekPriority(args->delayQueue);
    if (firstItem > 0LL) {
      apr_interval_time_t newWait = firstItem - apr_time_now();
      if (newWait <= 0) {
        waitTime = 0;
      } else if (newWait < waitTime) {
        waitTime = newWait;
      }
    }

    apr_pollset_poll(pollSet, waitTime, &pollSize, &pollResult);
#if DEBUG
    printf("Polled %i result\n", pollSize);
#endif

    /* Process any pending I/O */
    for (i = 0; i < pollSize; i++) {
      ConnectionInfo* conn = (ConnectionInfo*)pollResult[i].client_data;
      ps = processConnection(conn, &(polls[conn->pollIndex]),
			     myAddr, memPool);
#if DEBUG
      printf("processConnection = %i\n", ps);
#endif
      if (ps > 0) {
        /* Want to wait for more I/O */
        ps |= (APR_POLLHUP | APR_POLLERR);
        if (ps != pollResult[i].reqevents) {
          polls[conn->pollIndex].rtnevents = 0;
          polls[conn->pollIndex].reqevents = ps;
	  apr_pollset_remove(pollSet, &(pollResult[i]));
	  apr_pollset_add(pollSet, &(polls[conn->pollIndex]));
#if DEBUG
	  printf("Re-added poll with flags %i\n", ps);
#endif
        }
      } else if (ps < 0) {
        /* Panic */
	goto done;
      } else {
        /* Wants to yield */
        apr_pollset_remove(pollSet, &(pollResult[i]));
        pq_Push(args->delayQueue, conn, 
                apr_time_now() + (conn->delayMillis * 1000LL));
#if DEBUG
	printf("Delaying by %i millis\n", conn->delayMillis);
#endif
      }
    }
  
    /* Process any delays in the queue */
    now = apr_time_now();
    firstItem = pq_PeekPriority(args->delayQueue);
 
    while ((firstItem > 0LL) && (firstItem <= now)) {
#if DEBUG
      printf("Processing a delayed item\n");
#endif
      ConnectionInfo* conn = (ConnectionInfo*)pq_Pop(args->delayQueue);
      ps = processConnection(conn, &(polls[conn->pollIndex]),
                              myAddr, memPool);
#if DEBUG
      printf("processConnection = %i\n", ps);
#endif
      if (ps > 0) {
        /* Want to wait for more I/O */
        ps |= (APR_POLLHUP | APR_POLLERR);
        polls[conn->pollIndex].rtnevents = 0;
        polls[conn->pollIndex].reqevents = ps;
        apr_pollset_add(pollSet, &(polls[conn->pollIndex]));
#if DEBUG
        printf("Re-added poll with flags %i\n", ps);
#endif
      } else if (ps < 0) {
        /* Panic */
	goto done;
      } else {
        pq_Push(args->delayQueue, conn, 
                now + (conn->delayMillis * 1000LL));
#if DEBUG
	printf("Delaying by %i millis\n", conn->delayMillis);
#endif
      }
        
      firstItem = pq_PeekPriority(args->delayQueue);
    }
  }

#if DEBUG
  printf("Thread read count = %lu write count = %lu\n",
         args->readCount, args->writeCount);
#endif

  for (i = 0; i < args->numConnections; i++) {
    cleanupConnection(&(conns[i]));
  }

  apr_pollset_destroy(pollSet);

  done:
    apr_pool_destroy(memPool);

}
