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
#include <rtl-sdr.h>
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
  int listen_sock;
  int cli_sock;
  rtlsdr_rpc_msg_t query_msg;
  rtlsdr_rpc_msg_t reply_msg;
  rtlsdr_rpc_msg_t sampl_msg;
  rtlsdr_rpc_msg_t event_msg;
} serv_t;

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

static int open_nonblock_socket
(
 struct sockaddr_storage saddr_both[2], size_t size_both[2],
 int type, int proto,
 struct sockaddr_storage** saddr_used, size_t* size_used
)
{
  size_t i;
  int fd = -1;

  for (i = 0; (i != 2) && (size_both[i]); ++i)
  {
    const struct sockaddr* const sa = (const struct sockaddr*)&saddr_both[i];
    fd = socket(sa->sa_family, type, proto);
    if (fd != -1) break ;
  }

  if ((i == 2) || (size_both[i] == 0)) return -1;

  *saddr_used = &saddr_both[i];
  *size_used = size_both[i];

  if (fcntl(fd, F_SETFL, O_NONBLOCK))
  {
    close(fd);
    return -1;
  }

  return fd;
}

static int init_serv(serv_t* serv, const char* addr, const char* port)
{
  struct sockaddr_storage saddrs[2];
  struct sockaddr_storage* saddr;
  size_t sizes[2];
  size_t size;
  int err;
  int enable = 1;

  /* TODO: handle errors */
  rtlsdr_rpc_msg_init(&serv->query_msg, 0);
  rtlsdr_rpc_msg_init(&serv->reply_msg, 0);
  rtlsdr_rpc_msg_init(&serv->sampl_msg, 0);
  rtlsdr_rpc_msg_init(&serv->event_msg, 0);

  if (resolve_ip_addr(saddrs, sizes, addr, port))
  {
    ERROR();
    goto on_error_0;
  }

  serv->listen_sock = open_nonblock_socket
    (saddrs, sizes, SOCK_STREAM, IPPROTO_TCP, &saddr, &size);
  if (serv->listen_sock == -1)
  {
    ERROR();
    goto on_error_0;
  }

  err = setsockopt
    (serv->listen_sock, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(int));
  if (err)
  {
    ERROR();
    goto on_error_1;
  }

  err = bind
    (serv->listen_sock, (const struct sockaddr*)saddr, (socklen_t)size);
  if (err)
  {
    ERROR();
    goto on_error_1;
  }

  if (listen(serv->listen_sock, 5))
  {
    ERROR();
    goto on_error_1;
  }

  serv->cli_sock = -1;

  return 0;

 on_error_1:
  close(serv->listen_sock);
 on_error_0:
  return -1;
}

static int fini_serv(serv_t* serv)
{
  if (serv->cli_sock != -1)
  {
    shutdown(serv->cli_sock, SHUT_RDWR);
    close(serv->cli_sock);
  }

  shutdown(serv->listen_sock, SHUT_RDWR);
  close(serv->listen_sock);

  rtlsdr_rpc_msg_fini(&serv->query_msg);
  rtlsdr_rpc_msg_fini(&serv->reply_msg);
  rtlsdr_rpc_msg_fini(&serv->sampl_msg);
  rtlsdr_rpc_msg_fini(&serv->event_msg);

  return 0;
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

  if (recv_all(fd, (uint8_t*)&size, sizeof(uint32_t)))
  {
    ERROR();
    return -1;
  }

  rtlsdr_rpc_msg_set_size(m, size);
  size = rtlsdr_rpc_msg_get_size(m);

  if (rtlsdr_rpc_msg_realloc(m, size))
  {
    ERROR();
    return -1;
  }

  if (recv_all(fd, m->fmt + sizeof(uint32_t), size - sizeof(uint32_t)))
  {
    ERROR();
    return -1;
  }

  return 0;
}

static int send_msg(int fd, rtlsdr_rpc_msg_t* m)
{
  return send_all(fd, m->fmt, m->off);
}

static int recv_query(serv_t* serv, rtlsdr_rpc_msg_t** q)
{
  *q = NULL;
  rtlsdr_rpc_msg_reset(&serv->query_msg);
  if (recv_msg(serv->cli_sock, &serv->query_msg)) return -1;
  *q = &serv->query_msg;
  return 0;
}

static int send_reply(serv_t* serv, rtlsdr_rpc_msg_t* r)
{
  TRACE();

  return send_msg(serv->cli_sock, r);
}

static int handle_query
(serv_t* serv, rtlsdr_rpc_msg_t* q, rtlsdr_rpc_msg_t** rr)
{
  rtlsdr_rpc_msg_t* const r = &serv->reply_msg;
  rtlsdr_rpc_op_t op;
  uint8_t err = 0;

  TRACE();

  *rr = NULL;

  rtlsdr_rpc_msg_reset(r);

  op = rtlsdr_rpc_msg_get_op(q);
  switch (op)
  {
  case RTLSDR_RPC_OP_GET_DEVICE_COUNT:
    {
      uint32_t n;

      PRINTF("get_device_count()\n");

      n = rtlsdr_get_device_count();
      if (rtlsdr_rpc_msg_push_uint32(r, n)) goto on_error;
      break ;
    }

  case RTLSDR_RPC_OP_GET_DEVICE_NAME:
    {
      const char* s;
      uint32_t i;

      PRINTF("get_device_name()\n");

      if (rtlsdr_rpc_msg_pop_uint32(q, &i)) goto on_error;

      s = rtlsdr_get_device_name(i);
      if (s == NULL) s = "";

      if (rtlsdr_rpc_msg_push_str(r, s)) goto on_error;

      break ;
    }

  default:
    {
      PRINTF("invalid op: %u\n", op);
      goto on_error;
      break ;
    }
  }

  rtlsdr_rpc_msg_set_size(r, r->off);
  rtlsdr_rpc_msg_set_op(r, op);
  rtlsdr_rpc_msg_set_mid(r, rtlsdr_rpc_msg_get_mid(q));
  rtlsdr_rpc_msg_set_err(r, err);

  *rr = r;

 on_error:
  return 0;
}

static int do_serv(serv_t* serv)
{
  fd_set rset;
  int max_fd;

  while (1)
  {
    FD_ZERO(&rset);

    if (serv->cli_sock != -1)
    {
      FD_SET(serv->cli_sock, &rset);
      max_fd = serv->cli_sock;
    }
    else
    {
      FD_SET(serv->listen_sock, &rset);
      max_fd = serv->listen_sock;
    }

    if (select(max_fd + 1, &rset, NULL, NULL, NULL) <= 0)
    {
      ERROR();
      return -1;
    }

    if (FD_ISSET(serv->listen_sock, &rset))
    {
      PRINTF("new client\n");

      serv->cli_sock = accept(serv->listen_sock, NULL, NULL);
      if (serv->cli_sock == -1)
      {
	if ((errno == EWOULDBLOCK) || (errno == EAGAIN))
	  continue ;
	ERROR();
	break ;
      }

      if (fcntl(serv->cli_sock, F_SETFL, O_NONBLOCK))
      {
	ERROR();
	break ;
      }
    }
    else if (FD_ISSET(serv->cli_sock, &rset))
    {
      rtlsdr_rpc_msg_t* q;
      rtlsdr_rpc_msg_t* r;

      if (recv_query(serv, &q))
      {
	PRINTF("cli closed\n");
	shutdown(serv->cli_sock, SHUT_RDWR);
	close(serv->cli_sock);
	serv->cli_sock = -1;
      }
      else if (q != NULL)
      {
	handle_query(serv, q, &r);
	if (r != NULL) send_reply(serv, r);
      }
    }
  }

  return 0;
}

int main(int ac, char** av)
{
  serv_t serv;
  const char* addr;
  const char* port;

  addr = getenv("RTLSDR_SERV_ADDR");
  if (addr == NULL) addr = "127.0.0.1";

  port = getenv("RTLSDR_SERV_PORT");
  if (port == NULL) port = "40000";

  if (init_serv(&serv, addr, port)) return -1;
  do_serv(&serv);
  fini_serv(&serv);

  return 0;
}
