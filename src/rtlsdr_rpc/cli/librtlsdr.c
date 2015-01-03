#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <stddef.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <sys/types.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/fcntl.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <netdb.h>
#include <pthread.h>
#include "../common/rtlsdr_rpc.h"


#if 1
#include <stdio.h>
#define PRINTF(__s, ...) fprintf(stderr, __s, ##__VA_ARGS__)
#define TRACE() PRINTF("[t] %s,%u\n", __FILE__, __LINE__)
#define ERROR() PRINTF("[e] %s,%u\n", __FILE__, __LINE__)
#else
#define TRACE()
#define ERROR()
#define PRINTF(...)
#endif


typedef struct
{
  unsigned int is_init;
  uint16_t mid;
  int sock;
} rtlsdr_rpc_cli_t;

static rtlsdr_rpc_cli_t rtlsdr_rpc_cli = { 0, };

static int resolve_ip_addr
(
 struct sockaddr_storage saddr_both[2], size_t size_both[2],
 const char* addr, const char* port
)
{
  struct addrinfo ai;
  struct addrinfo* aip = NULL;
  int err = -1;
  size_t i;

  memset(&ai, 0, sizeof(ai));
  ai.ai_family = AF_UNSPEC;
  ai.ai_socktype = SOCK_STREAM;
  ai.ai_flags = AI_PASSIVE;

  if (getaddrinfo(addr, port, &ai, &aip)) goto on_error;

  size_both[0] = 0;
  size_both[1] = 0;
  i = 0;
  for (; (i != 2) && (aip != NULL); aip = aip->ai_next)
  {
    if ((aip->ai_family != AF_INET) && (aip->ai_family != AF_INET6)) continue ;
    if (aip->ai_addrlen == 0) continue ;
    memcpy(&saddr_both[i], aip->ai_addr, aip->ai_addrlen);
    size_both[i] = aip->ai_addrlen;
    ++i;
  }

  if (i == 0) goto on_error;

  err = 0;
 on_error:
  if (aip != NULL) freeaddrinfo(aip);
  return err;
}

static int open_socket
(
 struct sockaddr_storage saddr_both[2], size_t size_both[2],
 int type, int proto,
 struct sockaddr_storage** saddr_used, size_t* size_used
)
{
  size_t i;
  int fd;

  for (i = 0; (i != 2) && (size_both[i]); ++i)
  {
    const struct sockaddr* const sa = (const struct sockaddr*)&saddr_both[i];
    fd = socket(sa->sa_family, type, proto);
    if (fd != -1) break ;
  }

  if ((i == 2) || (size_both[i] == 0)) return -1;

  *saddr_used = &saddr_both[i];
  *size_used = size_both[i];

  return fd;
}

static int init_cli(rtlsdr_rpc_cli_t* cli)
{
  struct sockaddr_storage saddrs[2];
  struct sockaddr_storage* saddr;
  size_t sizes[2];
  size_t size;
  const char* addr;
  const char* port;

  if (cli->is_init) return 0;

  addr = getenv("RTLSDR_SERV_ADDR");
  if (addr == NULL) addr = "127.0.0.1";

  port = getenv("RTLSDR_SERV_PORT");
  if (port == NULL) port = "40000";

  if (resolve_ip_addr(saddrs, sizes, addr, port))
  {
    ERROR();
    goto on_error_0;
  }

  cli->sock = open_socket
    (saddrs, sizes, SOCK_STREAM, IPPROTO_TCP, &saddr, &size);
  if (cli->sock == -1)
  {
    ERROR();
    goto on_error_0;
  }

  if (connect(cli->sock, (const struct sockaddr*)saddr, (socklen_t)size))
  {
    ERROR();
    goto on_error_1;
  }

  if (fcntl(cli->sock, F_SETFL, O_NONBLOCK))
  {
    ERROR();
    goto on_error_1;
  }

  cli->mid = 0;

  cli->is_init = 1;

  return 0;

 on_error_1:
  close(cli->sock);
 on_error_0:
  return -1;
}

__attribute__((unused))
static int fini_cli(rtlsdr_rpc_cli_t* cli)
{
  shutdown(cli->sock, SHUT_RDWR);
  close(cli->sock);
  return 0;
}

static uint16_t get_mid(rtlsdr_rpc_cli_t* cli)
{
  return cli->mid++;
}

static int recv_all(int fd, uint8_t* buf, size_t size)
{
  ssize_t n;
  fd_set rset;

  while (1)
  {
    errno = 0;
    n = recv(fd, buf, size, 0);
    if (n <= 0)
    {
      if ((errno != EWOULDBLOCK) || (errno != EAGAIN))
	return -1;
    }
    else
    {
      size -= (size_t)n;
      buf += (size_t)n;
      if (size == 0) break ;
    }

    FD_ZERO(&rset);
    FD_SET(fd, &rset);
    if (select(fd + 1, &rset, NULL, NULL, NULL) <= 0)
    {
      return -1;
    }
  }

  return 0;
}

static int send_all(int fd, const uint8_t* buf, size_t size)
{
  ssize_t n;
  fd_set wset;

  while (1)
  {
    errno = 0;
    n = send(fd, buf, size, 0);
    if (n <= 0)
    {
      if ((errno != EWOULDBLOCK) || (errno != EAGAIN))
	return -1;
    }
    else
    {
      size -= (size_t)n;
      buf += (size_t)n;
      if (size == 0) break ;
    }

    FD_ZERO(&wset);
    FD_SET(fd, &wset);
    if (select(fd + 1, NULL, &wset, NULL, NULL) <= 0)
    {
      return -1;
    }
  }

  return 0;
}

static int recv_msg(int fd, rtlsdr_rpc_msg_t* m)
{
  uint32_t size;

  if (recv_all(fd, (uint8_t*)&size, sizeof(uint32_t))) return -1;
  rtlsdr_rpc_msg_set_size(m, size);
  size = rtlsdr_rpc_msg_get_size(m);
  if (rtlsdr_rpc_msg_realloc(m, size)) return -1;
  size -= sizeof(uint32_t);
  if (recv_all(fd, m->fmt + sizeof(uint32_t), size)) return -1;
  return 0;
}

static int send_msg(int fd, rtlsdr_rpc_msg_t* m)
{
  return send_all(fd, m->fmt, m->off);
}

static int send_recv_msg
(rtlsdr_rpc_cli_t* cli, rtlsdr_rpc_msg_t* q, rtlsdr_rpc_msg_t* r)
{
  rtlsdr_rpc_msg_set_mid(q, get_mid(cli));
  rtlsdr_rpc_msg_set_size(q, (uint32_t)q->off);
  if (send_msg(cli->sock, q)) return -1;

  if (recv_msg(cli->sock, r)) return -1;

  return 0;
}

uint32_t rtlsdr_rpc_get_device_count(void)
{
  rtlsdr_rpc_cli_t* const cli = &rtlsdr_rpc_cli;

  rtlsdr_rpc_msg_t q;
  rtlsdr_rpc_msg_t r;
  uint32_t n = 0;

  if (init_cli(cli)) goto on_error_0;

  rtlsdr_rpc_msg_init(&q, 0);
  rtlsdr_rpc_msg_init(&r, 0);

  rtlsdr_rpc_msg_set_op(&q, RTLSDR_RPC_OP_GET_DEVICE_COUNT);
  if (send_recv_msg(cli, &q, &r)) goto on_error_1;

  if (rtlsdr_rpc_msg_pop_uint32(&r, &n)) goto on_error_1;

 on_error_1:
  rtlsdr_rpc_msg_fini(&r);
  rtlsdr_rpc_msg_fini(&q);
 on_error_0:
  return n;
}

const char* rtlsdr_rpc_get_device_name
(
 uint32_t index
)
{
  rtlsdr_rpc_cli_t* const cli = &rtlsdr_rpc_cli;

  rtlsdr_rpc_msg_t q;
  rtlsdr_rpc_msg_t r;
  const char* s = NULL;

  if (init_cli(cli)) goto on_error_0;

  rtlsdr_rpc_msg_init(&q, 0);
  rtlsdr_rpc_msg_init(&r, 0);

  rtlsdr_rpc_msg_set_op(&q, RTLSDR_RPC_OP_GET_DEVICE_NAME);
  if (rtlsdr_rpc_msg_push_uint32(&q, index)) goto on_error_1;
  if (send_recv_msg(cli, &q, &r)) goto on_error_1;

  if (rtlsdr_rpc_msg_pop_str(&r, &s)) goto on_error_1;

  s = strdup(s);

 on_error_1:
  rtlsdr_rpc_msg_fini(&r);
  rtlsdr_rpc_msg_fini(&q);
 on_error_0:
  return s;
}
