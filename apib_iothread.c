#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

#include <apib.h>

#include <apr_network_io.h>
#include <apr_poll.h>
#include <apr_pools.h>
#include <apr_portable.h>
#include <apr_strings.h>

#include <openssl/err.h>
#include <openssl/ssl.h>

#define DEBUG 0

#define SEND_BUF_SIZE     65536

#define STATE_NONE          0
#define STATE_CONNECTING    1
#define STATE_SSL_HANDSHAKE 2
#define STATE_SENDING       3
#define STATE_RECV_READY    4
#define STATE_RECV_START    5
#define STATE_RECV_HDRS     6
#define STATE_RECV_BODY     7
#define STATE_SEND_READY    8
#define STATE_CLOSING       9
#define STATE_FAILED        10
#define STATE_PANIC         99

#define STATUS_WANT_READ  1
#define STATUS_WANT_WRITE 2
#define STATUS_CONTINUE   0

typedef struct {
  IOArgs*   ioArgs;
  apr_pool_t*     transPool;
  apr_pool_t*     connPool;
  apr_socket_t*   sock;
  int             isSsl;
  SSL*            ssl;
  int             state;
  int             bufPos;
  int             bufLen;
  int             sendBufPos;
  char*           buf;
  int             httpStatus;
  int             chunked;
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

  while (isspace(*ret)) {
    ret++;
  }

  len = strlen(s);
  while (isspace(ret[len - 1])) {
    ret[len - 1] = 0;
    len--;
  }

  return ret;
}

static void makeConnection(ConnectionInfo* conn, apr_pollfd_t* fd,
			   apr_sockaddr_t* addr, apr_pool_t* p)
{
  apr_status_t s;
  apr_socket_t* sock;

  apr_pool_create(&(conn->connPool), p);
  
  s = apr_socket_create(&sock, APR_INET, SOCK_STREAM, APR_PROTO_TCP, conn->connPool);
  if (s != APR_SUCCESS) {
    goto panic;
  }
  s = apr_socket_opt_set(sock, APR_SO_NONBLOCK, 1);
  if (s != APR_SUCCESS) {
    goto panic;
  }
  s = apr_socket_opt_set(sock, APR_SO_REUSEADDR, 1);
  if (s != APR_SUCCESS) {
    goto panic;
  }
/*
  s = apr_socket_opt_set(sock, APR_TCP_NODELAY, 1);
  if (s != APR_SUCCESS) {
    goto panic;
  }
*/

  if (conn->isSsl) {
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



static int setupConnection(ConnectionInfo* conn, apr_sockaddr_t* addr)
{
  apr_status_t s;

  s = apr_socket_connect(conn->sock, addr);

  if (s == APR_EINPROGRESS) {
    return STATUS_WANT_WRITE;
  } else if (s != APR_SUCCESS) {
    conn->panicStatus = s;
    SETSTATE(conn, STATE_PANIC);
  }
  
  if (conn->isSsl) {
    SETSTATE(conn, STATE_SSL_HANDSHAKE);
  } else {
    SETSTATE(conn, STATE_SEND_READY);
  }

  return STATUS_CONTINUE;
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
      conn->panicStatus = ERR_get_error();
      SETSTATE(conn, STATE_PANIC);
      return STATUS_CONTINUE;
    }
    assert(FALSE);
  }

  SETSTATE(conn, STATE_SEND_READY);
  return STATUS_CONTINUE;
}

static void cleanupConnection(ConnectionInfo* conn)
{
  if (conn->isSsl) {
    SSL_shutdown(conn->ssl);
    SSL_free(conn->ssl);
  }
  apr_socket_close(conn->sock);
  if (conn->transPool != NULL) {
    apr_pool_destroy(conn->transPool);
    conn->transPool = NULL;
  }
  if (conn->connPool != NULL) {
    apr_pool_destroy(conn->connPool);
    conn->connPool = NULL;
  }
}

static void buildRequest(ConnectionInfo* conn)
{
  char* path;

  conn->bufPos = 0;
  conn->bufLen = SEND_BUF_SIZE;
  conn->sendBufPos = 0;

  if (conn->ioArgs->url->path == NULL) {
    path = "/";
  } else {
    path = conn->ioArgs->url->path;
  }

  if (conn->ioArgs->url->query != NULL) {
    conn->bufPos += apr_snprintf(conn->buf, conn->bufLen - conn->bufPos, 
				 "%s %s?%s HTTP/1.1\r\n", conn->ioArgs->httpVerb,
				 path, conn->ioArgs->url->query);
  } else {
    conn->bufPos += apr_snprintf(conn->buf, conn->bufLen - conn->bufPos, 
				 "%s %s HTTP/1.1\r\n", conn->ioArgs->httpVerb, path);
  }

  conn->bufPos += apr_snprintf(conn->buf + conn->bufPos, conn->bufLen - conn->bufPos,
			       "User-Agent: %s\r\n", USER_AGENT);
  conn->bufPos += apr_snprintf(conn->buf + conn->bufPos, conn->bufLen - conn->bufPos,
			       "Host: %s\r\n", conn->ioArgs->url->hostname);

  if (conn->ioArgs->sendDataSize > 0) {
    char* cType = conn->ioArgs->contentType;

    conn->bufPos += apr_snprintf(conn->buf + conn->bufPos, conn->bufLen - conn->bufPos, 
				 "Content-Length: %i\r\n", conn->ioArgs->sendDataSize);
    if (cType == NULL) {
      cType = DEFAULT_CONTENT_TYPE;
    }
    conn->bufPos += apr_snprintf(conn->buf + conn->bufPos, conn->bufLen - conn->bufPos, 
				 "Content-Type: %s\r\n", cType);
  }
  conn->bufPos += apr_snprintf(conn->buf + conn->bufPos, conn->bufLen - conn->bufPos, 
			       "\r\n");

  assert(conn->bufPos <= conn->bufLen);

  conn->bufLen = conn->bufPos;
  conn->bufPos = 0;
  SETSTATE(conn, STATE_SENDING);
  conn->requestStart = apr_time_now();

  if (conn->ioArgs->verbose) {
    printf("%s", conn->buf);
  }
}

static int writeRequestSsl(ConnectionInfo* conn)
{
  int sslStatus;
  int sslErr;
  char buf[128];

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
    return STATUS_WANT_READ;
  }
  return STATUS_WANT_WRITE;
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
  printf("Headers: %i out of %i Body: %i out of %i sent %u\n",
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
  } else if ((conn->bufPos >= conn->bufLen) &&
	     (conn->sendBufPos >= conn->ioArgs->sendDataSize)) {
    /* Did all the writing -- ready to receive */
    SETSTATE(conn, STATE_RECV_READY);
    return STATUS_WANT_READ;
  }
  return STATUS_WANT_WRITE;
}

static int writeRequest(ConnectionInfo* conn)
{
  if (conn->isSsl) {
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

static void requestComplete(ConnectionInfo* conn)
{
  RecordResult(conn->ioArgs, conn->httpStatus, 
	       apr_time_now() - conn->requestStart);
}

static int readRequest(ConnectionInfo* conn)
{
  apr_size_t readLen;
  char* readBuf;
  char buf[128];

  linep_GetReadInfo(&(conn->line), &readBuf, &readLen);

  if (conn->isSsl) {
    int sslStatus;
    int sslErr;

#if DEBUG
    printf("Reading from SSL. State = %s Shutdown = %i\n", SSL_state_string_long(conn->ssl),
	   SSL_get_shutdown(conn->ssl));
#endif
    sslStatus = SSL_read(conn->ssl, readBuf, readLen);
    conn->ioArgs->readCount++;
    if (sslStatus <= 0) {
      sslErr = SSL_get_error(conn->ssl, sslStatus);
      switch (sslErr) {
      case SSL_ERROR_WANT_READ:
	return STATUS_WANT_READ;
      case SSL_ERROR_WANT_WRITE:
	return STATUS_WANT_WRITE;
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
      assert(TRUE);
    }

#if DEBUG
    printf("Read %i pending %i\n", sslStatus, SSL_pending(conn->ssl));
#endif
    readLen = sslStatus;

  } else {
    apr_status_t s;

    s = apr_socket_recv(conn->sock, readBuf, &readLen);
    conn->ioArgs->readCount++;
    if (APR_STATUS_IS_EAGAIN(s)) {
      return STATUS_WANT_WRITE;
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
      SETSTATE(conn, STATE_RECV_BODY);
      conn->contentRead = 0;
      break;
    } else {
      processHeader(conn);
    }
  }
  if (conn->state != STATE_RECV_BODY) {
    /* There are more lines to read and we haven't found a complete one yet */
    if (linep_Reset(&(conn->line))) {
      /* Line is too long for our buffer */
      SETSTATE(conn, STATE_PANIC);
    }
    return STATUS_CONTINUE;
  }

  if (conn->state == STATE_RECV_BODY) {
    /* Do different types of content length later */
    assert(conn->contentLength >= 0);
    linep_GetDataRemaining(&(conn->line), &readLen);
#if DEBUG
    printf("Read %i body bytes out of %i\n", readLen,
	   conn->contentLength);
#endif
    if (conn->ioArgs->verbose) {
      printf("  %i bytes of content\n", readLen);
    }
    conn->contentRead += readLen;
    if (conn->contentRead >= conn->contentLength) {
      /* Done reading -- can go back to sending! */
      requestComplete(conn);
      if (conn->closeRequested) {
	SETSTATE(conn, STATE_CLOSING);
      } else {
	SETSTATE(conn, STATE_SEND_READY);
      }
    } else {
      /* Need to read more from the body */
      linep_Start(&(conn->line), conn->buf, SEND_BUF_SIZE, 0);
    }
  }
  return STATUS_CONTINUE;
}

static int processConnection(ConnectionInfo* conn, apr_pollfd_t* poll, 
                             apr_sockaddr_t* addr, apr_pool_t* p)
{
  char errBuf[128];

  if (poll->rtnevents & (APR_POLLERR | APR_POLLHUP)) {
#if DEBUG
    printf("Connection error from poll\n");
#endif
    cleanupConnection(conn);
    makeConnection(conn, poll, addr, p);
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
      makeConnection(conn, poll, addr, p);
      break; 

    case STATE_FAILED: 
    case STATE_CLOSING:
      cleanupConnection(conn);
      makeConnection(conn, poll, addr, p);
      break;
      
    case STATE_CONNECTING:
      s = setupConnection(conn, addr);
      break;

    case STATE_SSL_HANDSHAKE:
      s = handshakeSsl(conn);
      break;

    case STATE_SEND_READY:
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
    default:
      /* Continue looping */
      break;
    }
  }
}


void RunIO(IOArgs* args)
{
  apr_pool_t*       memPool;
  apr_sockaddr_t*   addr;
  apr_pollset_t*    pollSet;
  apr_status_t      s;
  char              errBuf[128];
  int               port;
  ConnectionInfo*   conns;
  apr_pollfd_t*     polls;
  int               i;
  int               p;
  int               ps;
  int               isSsl;
  int               pollSize;
  const apr_pollfd_t*     pollResult;
  
  apr_pool_create(&memPool, MainPool);

  args->readCount = args->writeCount = 0;

  if (!strcmp(args->url->scheme, "https")) {
    isSsl = TRUE;
  } else {
    isSsl = FALSE;
    assert(!strcmp(args->url->scheme, "http"));
  }

  if (args->url->port_str == NULL) {
    if (isSsl) {
      port = 443;
    } else {
      port = 80;
    }
  } else {
    port = atoi(args->url->port_str);
  }

  s = apr_sockaddr_info_get(&addr, args->url->hostname,
			    APR_INET, port, 0, memPool);
  if (s != APR_SUCCESS) {
    apr_strerror(s, errBuf, 80);
    fprintf(stderr, "Error looking up host: %s\n", errBuf);
    goto done;
  }

  s = apr_pollset_create(&pollSet, args->numConnections, memPool, APR_POLLSET_NOCOPY);
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
    conns[i].ioArgs = args;
    conns[i].transPool = NULL;
    conns[i].connPool = NULL;
    conns[i].buf = (char*)apr_palloc(memPool, SEND_BUF_SIZE);
    conns[i].state = STATE_NONE;
    conns[i].wakeups = 0;
    conns[i].isSsl = isSsl;

    polls[i].p = memPool;
    polls[i].desc_type = APR_POLL_SOCKET;
    polls[i].reqevents = polls[i].rtnevents = 0;
    polls[i].client_data = (void*)i;

    ps = processConnection(&(conns[i]), &(polls[i]), addr, memPool);
    if (ps >= 0) {
	polls[i].reqevents = ps | (APR_POLLHUP | APR_POLLERR);
	apr_pollset_add(pollSet, &(polls[i]));
    } else {
      goto done;
    }
  }

  while (Running) {
    apr_pollset_poll(pollSet, -1, &pollSize, &pollResult);
#if DEBUG
    printf("Polled %i result\n", pollSize);
#endif
    for (i = 0; i < pollSize; i++) {
      p = (int)pollResult[i].client_data;
      ps = processConnection(&(conns[p]), &(polls[p]),
			     addr, memPool);
      if (ps >= 0) {
        ps |= (APR_POLLHUP | APR_POLLERR);
        if (ps != pollResult[i].reqevents) {
          polls[p].rtnevents = 0;
          polls[p].reqevents = ps;
	  apr_pollset_remove(pollSet, &(pollResult[i]));
	  apr_pollset_add(pollSet, &(polls[p]));
        }
      } else {
	goto done;
      }
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
