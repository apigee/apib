/*
 * This is a program that returns CPU information over the network.
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include <apr_file_io.h>
#include <apr_general.h>
#include <apr_network_io.h>
#include <apr_pools.h>
#include <apr_strings.h>
#include <apr_thread_proc.h>

#include <apib_common.h>

#define LISTEN_BACKLOG 8
#define READ_BUF_LEN 128
#define PROC_BUF_LEN 512

static apr_pool_t* MainPool;
static double TicksPerSecond;
static int CPUCount;

typedef struct {
  apr_socket_t* sock;
  apr_pool_t* pool;
} ThreadArgs;

typedef struct {
  int valid;
  long long user;
  long long nice;
  long long system;
  long long idle;
  long long ioWait;
} CPUTicks;

static int isCompleteLine(const char* s)
{
  return ((strchr(s, '\n') != NULL) || (strchr(s, '\r') != NULL));
}

static void sendBack(apr_socket_t* sock, const char* msg)
{
  unsigned int len = strlen(msg);
  
  apr_socket_send(sock, msg, &len);
}

static void sendTickInfo(apr_socket_t* sock, CPUTicks* ticks,
			 double uptime)
{
  char buf[256];
  double user;
  double nice;
  double system;
  double idle;
  double io;
  double uptimeTicks = (uptime * TicksPerSecond) * CPUCount;

  user = (double)ticks->user / uptimeTicks * 100.0;
  nice = (double)ticks->nice / uptimeTicks * 100.0;
  system = (double)ticks->system / uptimeTicks * 100.0;
  idle = (double)ticks->idle / uptimeTicks * 100.0;
  io = (double)ticks->ioWait / uptimeTicks * 100.0;

  apr_snprintf(buf, 256, "%.2lf%%u %.2lf%%n %.2lf%%s %.2lf%%i %.2lf%%w\n",
	       user, nice, system, idle, io);
  sendBack(sock, buf);
}

static void setLL(LineState* line, long long* val) 
{
  char* tok = linep_NextToken(line, " \t");
  if (tok != NULL) {
    *val = atoll(tok);
  }
}

static double getUptime(ThreadArgs* args)
{
  apr_status_t s;
  apr_file_t* proc;
  char buf[PROC_BUF_LEN];
  LineState line;

  s = apr_file_open(&proc, "/proc/uptime", APR_READ, APR_OS_DEFAULT, args->pool);
  if (s != APR_SUCCESS) {
    return 0.0;
  }

  linep_Start(&line, buf, PROC_BUF_LEN, 0);
  s = linep_ReadFile(&line, proc);
  apr_file_close(proc);
  if (s != APR_SUCCESS) {
    return 0.0;
  }

  if (linep_NextLine(&line)) {
    char* tok = linep_NextToken(&line, " \r\n\t");
    if (tok != NULL) {
      return strtod(tok, NULL);
    }
  }
  return 0.0;
}

static void getCPU(ThreadArgs* args, CPUTicks* ticks)
{
  apr_status_t s;
  apr_file_t* proc;
  char buf[PROC_BUF_LEN];
  LineState line;
  
  ticks->valid = 0;

  s = apr_file_open(&proc, "/proc/stat", APR_READ, APR_OS_DEFAULT, args->pool);
  if (s != APR_SUCCESS) {
    return;
  }

  linep_Start(&line, buf, PROC_BUF_LEN, 0);
  s = linep_ReadFile(&line, proc);
  apr_file_close(proc);
  if (s != APR_SUCCESS) {
    return;
  }

  while (linep_NextLine(&line)) {
    char* l = linep_GetLine(&line);
    if (!strncmp(l, "cpu ", 4)) {
      linep_NextToken(&line, " \t");
      setLL(&line, &(ticks->user));
      setLL(&line, &(ticks->nice));
      setLL(&line, &(ticks->system));
      setLL(&line, &(ticks->idle));
      setLL(&line, &(ticks->ioWait));
      ticks->valid = 1;
      break;
    }
  }
}

static int processCommand(ThreadArgs* args, const char* cmd)
{
  if (!strcasecmp(cmd, "HELLO")) {
    sendBack(args->sock, "Hi!\n");
  } else if (!strcasecmp(cmd, "START")) {
    sendBack(args->sock, "OK\n");
  } else if (!strcasecmp(cmd, "CPU")) {
    CPUTicks ticks;
    double uptime;

    getCPU(args, &ticks);
    uptime = getUptime(args);
    if (ticks.valid && (uptime > 0.0)) {
      sendTickInfo(args->sock, &ticks, uptime);
    } else {
      sendBack(args->sock, "ERROR\n");
    }
  } else if (!strcasecmp(cmd, "BYE") || !strcasecmp(cmd, "QUIT")) {
    sendBack(args->sock, "BYE\n");
    return 1;
  } else {
    sendBack(args->sock, "Invalid command\n");
  }
  return 0;
}

static void* SocketThread(apr_thread_t* t, void* a)
{
  ThreadArgs* args = (ThreadArgs*)a;
  char* readBuf = apr_palloc(args->pool, READ_BUF_LEN);
  unsigned int readPos = 0;
  unsigned int readLen;
  apr_status_t s;
  int completeLine;
  int closeRequested = FALSE;

  while (!closeRequested) {
    completeLine = FALSE;
    readLen = READ_BUF_LEN - readPos;
    /* Loop until we have a whole line from the client, or error, or too far */
    do {
      s = apr_socket_recv(args->sock, readBuf + readPos, &readLen);
      if (s != APR_SUCCESS) {
	break;
      }
      readPos += readLen;
      completeLine = isCompleteLine(readBuf);
    } while ((s == APR_SUCCESS) && (readLen < READ_BUF_LEN) && !completeLine);

    if (completeLine) {
      unsigned int pos = 0;
      char* cmd = readBuf;

      do {
	unsigned int cmdLen = strcspn(cmd, "\r\n");
	pos += cmdLen;

	while ((pos < readPos) && 
	       ((readBuf[pos] == '\r') || (readBuf[pos] == '\n'))) {
	  readBuf[pos] = 0;
	  pos++;
	}
	
	closeRequested = processCommand(args, cmd);
	cmd = readBuf + pos;
	/* There may have been more commands read already -- read them */
	       
      } while ((pos < readPos) && isCompleteLine(cmd) && !closeRequested);

      /* In case there is data left, move it and read again */
      readPos = readPos - pos;
      memmove(readBuf, cmd, readPos);

    } else {
      /* We may have gone too far or the line was too long -- either way we're done */
      break;
    }
  }

  apr_socket_shutdown(args->sock, APR_SHUTDOWN_READWRITE);
  apr_socket_close(args->sock);
  apr_pool_destroy(args->pool);

  return NULL;
}

static void countCPUs(void)
{
  apr_status_t s;
  apr_file_t* f;
  char buf[PROC_BUF_LEN];
  LineState line;
  
  s = apr_file_open(&f, "/proc/cpuinfo", APR_READ, APR_OS_DEFAULT, MainPool);
  if (s != APR_SUCCESS) {
    printf("Can't count CPUs: We will assume that there are one\n");
    CPUCount = 1;
    return;
  }

  linep_Start(&line, buf, PROC_BUF_LEN, 0);
  do {
    s = linep_ReadFile(&line, f);
    if (s == APR_SUCCESS) {
      while (linep_NextLine(&line)) {
        char* l = linep_GetLine(&line);
	if (!strncmp(l, "processor ", 10) ||
	    !strncmp(l, "processor\t", 10)) {
	  CPUCount++;
	}
      }
      linep_Reset(&line);
    }
  } while (s == APR_SUCCESS);
  apr_file_close(f);

  printf("We counted %i CPUs\n", CPUCount);
  if (CPUCount < 1) {
    printf("Something is wrong: We will assume that there are one\n");
    CPUCount = 1;
  }
}

int main(int ac, char const* const* av)
{
  int argc = ac;
  char const* const* argv = av;
  char const * const* env = NULL;
  apr_socket_t* serverSock;
  apr_sockaddr_t* addr;
  apr_status_t s;
  char buf[128];
  
  apr_app_initialize(&argc, &argv, &env);
  apr_pool_create(&MainPool, NULL);

  if (argc != 2) {
    fprintf(stderr, "Usage: %s <port>\n", argv[0]);
    return 2;
  }

  s = apr_sockaddr_info_get(&addr, NULL, APR_INET, atoi(argv[1]), 0, MainPool);
  if (s != APR_SUCCESS) {
    goto failed;
  }

  s = apr_socket_create(&serverSock, APR_INET, SOCK_STREAM, APR_PROTO_TCP, MainPool);
  if (s != APR_SUCCESS) {
    goto failed;
  }

  s = apr_socket_opt_set(serverSock, APR_SO_REUSEADDR, TRUE);
  if (s != APR_SUCCESS) {
    goto failed;
  }

  s = apr_socket_bind(serverSock, addr);
  if (s != APR_SUCCESS) {
    goto failed;
  }

  s = apr_socket_listen(serverSock, LISTEN_BACKLOG);
  if (s != APR_SUCCESS) {
    goto failed;
  }

  countCPUs();
  TicksPerSecond = sysconf(_SC_CLK_TCK);

  while (TRUE) {
    apr_pool_t* socketPool;
    apr_thread_t* socketThread;
    apr_socket_t* clientSock;
    ThreadArgs* args;

    apr_pool_create(&socketPool, MainPool);
    s = apr_socket_accept(&clientSock, serverSock, socketPool);
    if (s != APR_SUCCESS) {
      apr_pool_destroy(socketPool);
      apr_strerror(s, buf, 128);
      fprintf(stderr, "Fatal error accepting client socket: %s\n", buf);
      continue;
    }

    args = (ThreadArgs*)apr_palloc(socketPool, sizeof(ThreadArgs));
    args->sock = clientSock;
    args->pool = socketPool;

    s = apr_thread_create(&socketThread, NULL, SocketThread, args, socketPool);
    if (s != APR_SUCCESS) { 
      apr_socket_close(clientSock);
      apr_pool_destroy(socketPool);
      apr_strerror(s, buf, 128);
      fprintf(stderr, "Fatal error creating socket thread: %s\n", buf);
      continue;
    }

    apr_thread_detach(socketThread);
  }

  apr_socket_close(serverSock);

  apr_pool_destroy(MainPool);
  apr_terminate();

  return 0;

 failed:
  apr_strerror(s, buf, 128);
  fprintf(stderr, "Fatal error: %s\n", buf);
  apr_pool_destroy(MainPool);
  apr_terminate();
  return 3;
}
