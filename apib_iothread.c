#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include <apib.h>

#include <apr_network_io.h>
#include <apr_poll.h>
#include <apr_pools.h>
#include <apr_strings.h>

#define DEBUG 0

#define SEND_BUF_SIZE     65536

#define STATE_NONE        0
#define STATE_CONNECTING  1
#define STATE_SENDING     2
#define STATE_RECV_READY  3
#define STATE_RECV_START  4
#define STATE_RECV_HDRS   5
#define STATE_RECV_BODY   6
#define STATE_SEND_READY  7
#define STATE_FAILED      8
#define STATE_PANIC       99

typedef struct {
  IOArgs*   ioArgs;
  apr_pool_t*     transPool;
  apr_pool_t*     connPool;
  apr_socket_t*   sock;
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
} ConnectionInfo;

#if DEBUG
#define SETSTATE(c, s) \
  printf("%lx %i -> %i (line %i)\n", (unsigned long)c, c->state, s, __LINE__);	\
  c->state = s;
#else
#define SETSTATE(c, s) \
  c->state = s;
#endif

static int makeConnection(ConnectionInfo* conn, apr_pollfd_t* fd,
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
  s = apr_socket_connect(sock, addr);

  SETSTATE(conn, STATE_CONNECTING);
  conn->sock = sock;
  fd->desc.s = sock;

  RecordConnectionOpen();

  if (s == APR_EINPROGRESS) {
    return 1;
  } else if (s != APR_SUCCESS) {
    goto panic;
  }
  return 0;

 panic:
  conn->panicStatus = s;
  SETSTATE(conn, STATE_PANIC);
  return 0;
}

static void cleanupConnection(ConnectionInfo* conn)
{
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

static int writeRequest(ConnectionInfo* conn)
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
  if (APR_STATUS_IS_EAGAIN(s)) {
    return 1;
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
  }
  return 0;
}

static void startReadRequest(ConnectionInfo* conn) 
{
  conn->bufPos = 0;
  conn->bufLen = SEND_BUF_SIZE;
  SETSTATE(conn, STATE_RECV_START);
}

static int readHttpLine(ConnectionInfo* conn, char* lineBuf)
{
  int cr = 0;
  int p = conn->bufPos;

  while (p < conn->bufLen) {
    if (conn->buf[p] == '\r') {
      cr = 1;
    } else if (cr && (conn->buf[p] == '\n')) {
      /* End of line */
      int len = p - conn->bufPos + 1;
      strncpy(lineBuf, conn->buf + conn->bufPos, len);
      lineBuf[len] = 0;
      return len;
    } else {
      cr = 0;
    }
    p++;
  }
  return 0;
}

static void processRequestLine(ConnectionInfo* conn, char* lineBuf)
{
  char* last;
  char* tok;
  
  /* Should get "HTTP/1.1". Check later? */
  apr_strtok(lineBuf, " \r\n", &last);
  tok = apr_strtok(NULL, " \r\n", &last);
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

static void processHeader(ConnectionInfo* conn, char* lineBuf)
{
  char* last;
  char* name;
  char* value;

  name = apr_strtok(lineBuf, " :", &last);
  value = apr_strtok(NULL, "\r\n", &last);

  if (value == NULL) {
    /* Another weird response so don't go on */
    return;
  }

  if (!strcmp(name, "Content-Length")) {
    conn->contentLength = atoi(value);
  } else if (!strcmp(name, "Connection") && !strcasecmp(value, "close")) {
    conn->closeRequested = 1;
  } else if (!strcmp(name, "Transfer-Encoding") && !strcasecmp(value, "chunked")) {
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
  apr_status_t s;
  apr_size_t read;
  char lineBuf[4096];
  int lineLen;

  read = conn->bufLen - conn->bufPos;

  s = apr_socket_recv(conn->sock, conn->buf + conn->bufPos, &read);
  conn->ioArgs->readCount++;
  if (APR_STATUS_IS_EAGAIN(s)) {
    return 1;
  }

  if (s != APR_SUCCESS) {
#if DEBUG
    apr_strerror(s, lineBuf, 4096);
    fprintf(stderr, "Receive error: %s\n", lineBuf);
#endif
    SETSTATE(conn, STATE_FAILED);
    RecordSocketError();
    return 0;
  }

  conn->bufPos = 0;
  conn->bufLen = read;

  while ((conn->state == STATE_RECV_START) || (conn->state == STATE_RECV_HDRS)) {
    lineLen = readHttpLine(conn, lineBuf);
    if (lineLen > 0) {
      /* Successfully read a line -- process it */
      if (conn->ioArgs->verbose) {
	printf("%s", lineBuf);
      }
      conn->bufPos += lineLen;
      if (conn->state == STATE_RECV_START) {
	processRequestLine(conn, lineBuf);
	SETSTATE(conn, STATE_RECV_HDRS);
      } else if (!strcmp(lineBuf, "\r\n")) {
	/* Blank line -- end of headers */
	SETSTATE(conn, STATE_RECV_BODY);
	conn->contentRead = 0;
      } else {
	processHeader(conn, lineBuf);
      }
    } else {
      /* Line incomplete -- copy and return for more later */
      memmove(conn->buf, conn->buf + conn->bufPos, conn->bufLen - conn->bufPos);
      conn->bufPos = conn->bufLen - conn->bufPos;
      conn->bufLen = SEND_BUF_SIZE;
      break;
    }
  }

  if (conn->state == STATE_RECV_BODY) {
    /* Do different types of content length later */
    assert(conn->contentLength >= 0);
#if DEBUG
    printf("Read %i body bytes out of %i\n", conn->bufLen - conn->bufPos, 
	   conn->contentLength);
#endif
    conn->contentRead += (conn->bufLen - conn->bufPos);
    if (conn->contentRead >= conn->contentLength) {
      /* Done reading -- can go back to sending! */
      requestComplete(conn);
      if (conn->closeRequested) {
	SETSTATE(conn, STATE_FAILED);
      } else {
	SETSTATE(conn, STATE_SEND_READY);
      }
    }
  }
  return 0;
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
    if (makeConnection(conn, poll, addr, p)) {
      return APR_POLLOUT;
    }
  }

  while (TRUE) {
    switch (conn->state) {
    case STATE_PANIC:
      apr_strerror(conn->panicStatus, errBuf, 128);
      fprintf(stderr, "PANIC: Unrecoverable error %i (%s). Thread exiting.\n",
	      conn->panicStatus, errBuf);
      return -1;
      
    case STATE_NONE:
      if (makeConnection(conn, poll, addr, p)) {
	return APR_POLLOUT;
      }
      break; 
    case STATE_FAILED: cleanupConnection(conn);
      if (makeConnection(conn, poll, addr, p)) {
	return APR_POLLOUT;
      }
      break;
      
    case STATE_CONNECTING:
    case STATE_SEND_READY:
      buildRequest(conn);
      break;

    case STATE_SENDING:
      if (writeRequest(conn)) {
        if (conn->state == STATE_RECV_READY) {
          return APR_POLLIN;
        } else {
	  return APR_POLLOUT;
        }
      }
      break;

    case STATE_RECV_READY:
      startReadRequest(conn);
      break;

    case STATE_RECV_START:
    case STATE_RECV_HDRS:
    case STATE_RECV_BODY:
      if (readRequest(conn)) {
	return APR_POLLIN;
      }
      break;
    
    default:
      fprintf(stderr, "Internal state error on poll result: %i\n", conn->state);
      cleanupConnection(conn);
      SETSTATE(conn, STATE_PANIC);
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
  int               pollSize;
  const apr_pollfd_t*     pollResult;
  
  apr_pool_create(&memPool, MainPool);

  args->readCount = args->writeCount = 0;

  if (args->url->port_str == NULL) {
    port = 80; /* TODO HTTPS */
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

    polls[i].p = memPool;
    polls[i].desc_type = APR_POLL_SOCKET;
    polls[i].reqevents = polls[i].rtnevents = 0;
    polls[i].client_data = i;

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

  printf("Thread read count = %lu write count = %lu\n",
         args->readCount, args->writeCount);

  apr_pollset_destroy(pollSet);

  done:
    apr_pool_destroy(memPool);

}
