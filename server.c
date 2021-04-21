#include <inttypes.h>
#include <librpma.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/epoll.h>
#include <unistd.h>
#include <libpmem.h>

#define USAGE_STR "usage: %s <server_address> <port>\n"

#include "common-conn.h"
#include "common-epoll.h"
#include "messages-ping-pong-common.h"
#include "server.h"
#include "deal_require.h"
#include "log.h"


/*
 * server_init -- initialize server's resources
 */
int server_init(struct server_res *svr, struct rpma_peer* peer)
{
  svr->epoll = epoll_create1(EPOLL_CLOEXEC);
  if (svr->epoll == -1) {
    return errno;
  }

  svr->peer = peer;

  return 0;
}

/*
 * server_fini -- release server's resources
 */
int server_fini(struct server_res *svr)
{
  /* close the epoll */
  if (close(svr->epoll)) {
    return errno;
  }

  return 0;
}

/*
 * client_new -- find a slot for the incoming client
 */
struct client_res* client_new(struct server_res *svr, struct rpma_conn_req *req)
{
  /* find the first free slot */
  struct client_res *clnt = NULL;
  for (int i = 0; i < CLIENT_MAX; ++i) {
    clnt = &svr->clients[i];
    if (clnt->conn != NULL)
      continue;
    LOG("client slot: %d", i);
    clnt->client_id = i;
    clnt->svr = svr;
    clnt->req = req;
    clnt->ev_conn_cmpl.fd  = -1;
    clnt->ev_conn_event.fd = -1;

    clnt->dst_ptr  = NULL;
    clnt->dst_mr   = NULL;
    clnt->dst_size = 0;
    clnt->is_pmem  = false;
    clnt->send_ptr = NULL;
    clnt->send_mr  = NULL;
    clnt->recv_ptr = NULL;
    clnt->recv_mr  = NULL;

    /* prepare send/recv memory and register the memory*/
    clnt->recv_ptr = malloc_aligned(MSG_SIZE);
    if (clnt->recv_ptr == NULL) {
      return NULL;
    }
    clnt->send_ptr = malloc_aligned(MSG_SIZE);
    if (clnt->send_ptr == NULL) {
      free(clnt->recv_ptr);
      clnt->recv_ptr = NULL;
      return NULL;
    }

    struct rpma_peer *peer = svr->peer;
    if (rpma_mr_reg(peer, clnt->recv_ptr, MSG_SIZE, RPMA_MR_USAGE_RECV, &clnt->recv_mr)){
      free(clnt->recv_ptr);
      clnt->recv_ptr = NULL;
      free(clnt->send_ptr);
      clnt->send_ptr = NULL;
      return NULL;
    }

    if (rpma_mr_reg(peer, clnt->send_ptr, MSG_SIZE, RPMA_MR_USAGE_SEND, &clnt->send_mr)){
      (void) rpma_mr_dereg(&clnt->recv_mr);
      free(clnt->recv_ptr);
      clnt->recv_ptr = NULL;
      free(clnt->send_ptr);
      clnt->send_ptr = NULL;
      return NULL;
    }

    break;
  }

  return clnt;
}

void client_handle_completion(struct custom_event* ce);
void client_handle_connection_event(struct custom_event* ce);

/*
 * client_add_to_epoll -- add client's file descriptors to epoll
 */
int client_add_to_epoll(struct client_res *clnt, int epoll) {
  /* get the connection's event fd and add it to epoll */
  int fd;
  int ret = rpma_conn_get_event_fd(clnt->conn, &fd);
  if (ret)
    return ret;
  ret = epoll_add(epoll, fd, clnt, client_handle_connection_event,
                  &clnt->ev_conn_event);
  if (ret)
    return ret;

  /* get the connection's completion fd and add it to epoll */
  ret = rpma_conn_get_completion_fd(clnt->conn, &fd);
  if (ret) {
    epoll_delete(epoll, &clnt->ev_conn_event);
    return ret;
  }
  ret = epoll_add(epoll, fd, clnt, client_handle_completion,
                  &clnt->ev_conn_cmpl);
  if (ret)
    epoll_delete(epoll, &clnt->ev_conn_event);

  return ret;
}

/*
 * client_delete -- release client's resources
 */
void client_delete(struct client_res *clnt) {
  LOG("solt: %d", clnt->client_id);
  struct server_res *svr = clnt->svr;

  if(clnt->dst_ptr) {
    char str[65];
    memcpy(str, clnt->dst_ptr, 64);
    str[64] = '\0';
    LOG("client.%d: %s\n", clnt->client_id, str);
  }

  if (clnt->ev_conn_cmpl.fd != -1) {
    epoll_delete(svr->epoll, &clnt->ev_conn_cmpl);
  }

  if (clnt->ev_conn_event.fd != -1) {
    epoll_delete(svr->epoll, &clnt->ev_conn_event);
  }

  /* deregister the memory region */
  if (clnt->send_mr != NULL) {
    rpma_mr_dereg(&clnt->send_mr);
    clnt->send_mr = NULL;
    free(clnt->send_ptr);
    clnt->send_ptr = NULL;
  }

  if (clnt->recv_mr != NULL) {
    rpma_mr_dereg(&clnt->recv_mr);
    clnt->recv_mr = NULL;
    free(clnt->recv_ptr);
    clnt->recv_ptr = NULL;
  }

  if(clnt->dst_mr != NULL) {
    rpma_mr_dereg(&clnt->dst_mr);
    LOG("is_pmem: %d\n", clnt->is_pmem);
    if(clnt->is_pmem) {
      pmem_unmap(clnt->dst_ptr, clnt->dst_size);
    } else {
      free(clnt->dst_ptr);
    }
    clnt->dst_ptr = NULL;
  }

  /* delete the connection and set conn to NULL */
  (void) rpma_conn_delete(&clnt->conn);
}

/*
 * client_handle_completion -- callback on completion is ready
 *
 */
void client_handle_completion(struct custom_event *ce)
{
  struct client_res *clnt = (struct client_res *)ce->arg;
  const struct server_res *svr = clnt->svr;

  /* prepare detected completions for processing */
  int ret = rpma_conn_completion_wait(clnt->conn);
  if (ret) {
    /* no completion is ready - continue */
    if (ret == RPMA_E_NO_COMPLETION) {
      return;
    } else if (ret == RPMA_E_INVAL) {
      LOG("conn is NULL: %s", rpma_err_2str(ret));
    } else if (ret == RPMA_E_PROVIDER) {
      LOG("ibv_poll_cq(3) failed with a provider error: %s", rpma_err_2str(ret));
    }

    /* another error occured - disconnect */
    (void) rpma_conn_disconnect(clnt->conn);
    return;
  }

  /* get next completion */
  struct rpma_completion cmpl;
  ret = rpma_conn_completion_get(clnt->conn, &cmpl);
  if (ret) {
    /* no completion is ready - continue */
    if (ret == RPMA_E_NO_COMPLETION) {
      return;
    } else if (ret == RPMA_E_INVAL) {
      LOG("conn or cmpl is NULL: %s", rpma_err_2str(ret));
    } else if (ret == RPMA_E_PROVIDER) {
      LOG("ibv_poll_cq(3) failed with a provider error: %s", rpma_err_2str(ret));
    } else if (ret == RPMA_E_UNKNOWN) {
      LOG("ibv_poll_cq(3) failed but no provider error is available: %s", rpma_err_2str(ret));
    } else {
      // RPMA_E_NOSUPP
      LOG("Not supported opcode: %s", rpma_err_2str(ret));
    }

    /* another error occured - disconnect */
    (void) rpma_conn_disconnect(clnt->conn);
    return;
  }

  /* validate received completion */
  if (cmpl.op_status != IBV_WC_SUCCESS) {
    (void) LOG("[%d]:[op: %d] received completion is not as expected (%d != %d)\n",
               clnt->client_id,
               cmpl.op,
               cmpl.op_status,
               IBV_WC_SUCCESS);

    return;
  }

  if (cmpl.op == RPMA_OP_RECV) {
    //TODO: solve the recv condition
    //1. solve the information based on receive
    //2. prepare a receive for the client's response;
    //3. [optional] response the client
    if (cmpl.op_context != clnt->recv_ptr || cmpl.byte_len != MSG_SIZE) {
      (void) LOG("received completion is not as expected (%p != %p [cmpl.op_context] || %"
                 PRIu32
                 " != %d [cmpl.byte_len] )",
                 cmpl.op_context, clnt->recv_ptr,
                 cmpl.byte_len, MSG_SIZE);
      return;
    }
    LOG("RPMA_OP_RECV");
    deal_require(clnt);
  } else if ( cmpl.op == RPMA_OP_SEND) {
    LOG("RPMA_OP_SEND");
    //TODO: solve the send condition after send successfully
    //now, don't do any thing
  } else {
    LOG("operation: %d\n. Shouldn't step in this", cmpl.op);
  }

  return;
}


/*
 * client_handle_connection_event -- callback on connection's next event
 */
void client_handle_connection_event(struct custom_event *ce)
{
  struct client_res *clnt = (struct client_res *)ce->arg;

  /* get next connection's event */
  enum rpma_conn_event event;
  int ret = rpma_conn_next_event(clnt->conn, &event);
  if (ret) {
    if (ret == RPMA_E_NO_EVENT) {
      return;
    } else if (ret == RPMA_E_INVAL) {
      LOG("conn or event is NULL");
    } else if (ret == RPMA_E_UNKNOWN) {
      LOG("unexpected event");
    } else if (ret == RPMA_E_PROVIDER) {
      LOG("rdma_get_cm_event() or rdma_ack_cm_event() failed");
    } else if (ret == RPMA_E_NOMEM) {
      LOG("out of memory");
    }

    (void) rpma_conn_disconnect(clnt->conn);
    return;
  }

  /* proceed to the callback specific to the received event */
  if (event == RPMA_CONN_ESTABLISHED) {
    //don't do anythings if no private data
    LOG("RPMA_CONN_ESTABLISHED");
    return;
  } else if (event == RPMA_CONN_CLOSED) {
    LOG("RPMA_CONN_CLOSED");
  } else if (event == RPMA_CONN_LOST) {
    LOG("RPMA_CONN_LOST");
  } else {
    //RPMA_CONN_UNDEFINED
    LOG("RPMA_CONN_UNDEFINED");
  }

  client_delete(clnt);
}

/*
 * server_handle_incoming_client -- callback on endpoint's next incoming
 * connection
 *
 * Get the connection request. If there is not free slots reject it. Otherwise,
 * accept the incoming connection, get the event and completion file
 * descriptors, set O_NONBLOCK flag for both of them and add events to
 * the epoll.
 * If error will occur at any of the required steps the client is disconnected.
 */
void server_handle_incoming_client(struct custom_event *ce)
{
  struct server_res *svr = (struct server_res *)ce->arg;

  /* receive an incoming connection request */
  struct rpma_conn_req *req = NULL;
  if (rpma_ep_next_conn_req(svr->ep, NULL, &req)) {
    return;
  }

  /* if no free slot is available */
  struct client_res *clnt = NULL;
  if ((clnt = client_new(svr, req)) == NULL) {
    rpma_conn_req_delete(&req);
    return;
  }

  /*
   * Put an initial receive to be prepared for the first message of
   * the client's ping-pong.
   */
  if (rpma_conn_req_recv(req, clnt->recv_mr, 0, MSG_SIZE, clnt->recv_ptr)) {
    (void) rpma_conn_req_delete(&req);
    return;
  }

  /* accept the connection request and obtain the connection object */
  if (rpma_conn_req_connect(&req, NULL, &clnt->conn)) {
    (void) rpma_conn_req_delete(&req);
    /*
     * When rpma_conn_req_connect() fails the connection pointer
     * remains unchanged (in this case it is NULL) so the server
     * would choose the same client slot if another client will
     * come. No additional cleanup needed.
     */
    return;
  }

  if (client_add_to_epoll(clnt, svr->epoll)) {
    (void) rpma_conn_disconnect(clnt->conn);
  }
}

int main(int argc, char* argv[]) {
  /* validate parameters */
  if (argc < 3) {
    fprintf(stderr, USAGE_STR, argv[0]);
    exit(-1);
  }

#ifdef USE_LIBPMEM
  LOG("using pmem\n");
#else
  LOG("Don't use pmem!\n");
#endif

  /* configure logging thresholds to see more details */
  rpma_log_set_threshold(RPMA_LOG_THRESHOLD, RPMA_LOG_LEVEL_INFO);
  rpma_log_set_threshold(RPMA_LOG_THRESHOLD_AUX, RPMA_LOG_LEVEL_INFO);

  /* read common parameters */
  char *addr = argv[1];
  char *port = argv[2];
  int ret = 0;

  /* RPMA resources - general */
  struct rpma_peer *peer = NULL;


  /* server resource */
  struct server_res svr = {0};

  /*
   * lookup an ibv_context via the address and create a new peer using it
   */
  if ((ret = server_peer_via_address(addr, &peer))) {
    return ret;
  }

  /* initialize the server's structure */
  if (ret = server_init(&svr, peer)) {
    goto err_peer_delete;
  }

  /* start a listening endpoint at addr:port */
  if (ret = rpma_ep_listen(peer, addr, port, &svr.ep)) {
    goto err_server_fini;
  }


  /* get the endpoint's event file descriptor and add it to epoll */
  int ep_fd;
  if (ret = rpma_ep_get_fd(svr.ep, &ep_fd)) {
    goto err_ep_shutdown;
  }
  ret = epoll_add(svr.epoll, ep_fd, &svr, server_handle_incoming_client,
                  &svr.ev_incoming);
  if (ret)
          goto err_ep_shutdown;


  /* process epoll's events */
  struct epoll_event event;
  struct custom_event *ce;
  while ((ret = epoll_wait(svr.epoll, &event, 1 /* # of events */,
                                TIMEOUT_1500S)) == 1) {
    ce = (struct custom_event *)event.data.ptr;
    ce->func(ce);
  }

  /* disconnect all remaining client's */
  for (int i = 0; i < CLIENT_MAX; ++i) {
    if (svr.clients[i].conn == NULL) {
      continue;
    }

    (void) rpma_conn_disconnect(svr.clients[i].conn);
  }

  if (ret == 0) {
    (void) LOG("Server timed out.");
  }

err_ep_shutdown:
  /* shutdown the endpoint */
  ret |= rpma_ep_shutdown(&svr.ep);

err_server_fini:
  /* release the server's resources */
  ret |= server_fini(&svr);

err_peer_delete:
  /* delete the peer object */
  ret |= rpma_peer_delete(&peer);

  return ret;
}

