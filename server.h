#ifndef SERVER_H
#define SERVER_H

#define USE_LIBPMEM

#include "common-conn.h"
#include "common-epoll.h"
#include "messages-ping-pong-common.h"


#define CLIENT_MAX 10

struct client_res {
  /* RPMA resources */
  struct rpma_conn_req *req;
  struct rpma_conn* conn;

  /* resources - memory regions */
  void* dst_ptr;
  uint64_t dst_size;
  int is_pmem;
  struct rpma_mr_local* dst_mr; // used to read/write
  void* send_ptr;
  struct rpma_mr_local* send_mr;
  void* recv_ptr;
  struct rpma_mr_local* recv_mr;

  /* events */
  struct custom_event ev_conn_event;
  struct custom_event ev_conn_cmpl;

  /* parent and identifier */
  struct server_res* svr;
  //TODO:后期使用链表?
  int client_id;
};

struct server_res {
  /* RPMA resources */
  struct rpma_peer *peer;
  struct rpma_ep *ep;

  /* epoll and event */
  int epoll;
  struct custom_event ev_incoming;

  /* client's resources */
  // TODO: 改成链表数据结构?
  struct client_res clients[CLIENT_MAX];
};


#endif // SERVER_H
