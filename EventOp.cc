#include "EventHandler.h"
#include "EventOp.h"

#include <inttypes.h>
#include <librpma.h>
#include <iostream>
#include <assert.h>
#include <string>
#include <errno.h>
#include <memory>

#include "common-conn.h"
#include "log.h"
#include "messages-ping-pong-common.h"


//#define MSG_SIZE 4096

AcceptorHandler::AcceptorHandler(const std::string& addr,
                   const std::string& port,
                   const std::weak_ptr<Reactor> reactor_manager)
  : EventHandlerInterface(reactor_manager), _address(addr), _port(port)
 {

  // Register acceptor with the Initiation Dispatcher,
  // which "double dispatches" the RPMA_Acceptor::get_handle()
  // method to obtain the Handle.
  // RPMA_Reactor::instance()->register_handler(this, ACCEPT_EVENT);
  std::cout << "I'm in AcceptorHandler:AcceptorHandler()" << std::endl;
  int ret = 0;
  struct rpma_peer *peer = nullptr;
  ret = server_peer_via_address(addr.c_str(), &peer);
  if (ret) {
    throw std::runtime_error("lookup an ibv_context via the address and create a new peer using it failed");
  }
  _peer.reset(peer, RpmaPeerDeleter());

  struct rpma_ep *ep = nullptr;
  ret = rpma_ep_listen(peer, addr.c_str(), port.c_str(), &ep);
  if (ret) {
    throw std::runtime_error("listening endpoint at addr:port failed");
  }
  _ep.reset(ep);
  
  Handle fd;
  ret = rpma_ep_get_fd(ep, &fd);
  if (ret) {
    throw std::runtime_error("get the endpoint's event file descriptor failed");
  }
  _fd.reset(new int(fd));
}

int AcceptorHandler::register_self() {
  if (auto reactor = _reactor_manager.lock()) {
    return reactor->register_handler(shared_from_this(), ACCEPT_EVENT);
  }
  return -1;
}

AcceptorHandler::~AcceptorHandler() {
  std::cout << "I'm in AcceptorHandler::~AcceptorHandler()" << std::endl;
}

Handle AcceptorHandler::get_handle(EventType et) const {
  return *_fd;
}

int AcceptorHandler::handle(EventType et) {
  // Can only be called for an ACCEPT event.
  assert(et == ACCEPT_EVENT);

  // Accept the connection.
  // Create a new RPMA_Handler.
  // registers itself with the Reactor
  try {
    std::shared_ptr<RPMAHandler> client_handler = std::make_shared<RPMAHandler>(_peer, _ep.get(), _reactor_manager);
    client_handler->register_self();
  } catch (std::runtime_error &e) {
    std::cout << "Runtime error: " << e.what() << std::endl;
    return -1;
  }
  return 0;
}



RPMAHandler::RPMAHandler(std::shared_ptr<struct rpma_peer> peer,
                         struct rpma_ep *ep,
                         const std::weak_ptr<Reactor> reactor_manager)
  : EventHandlerInterface(reactor_manager), _peer(peer) {
  std::cout << "I'm in RPMAHandler::RPMAHandler()" << std::endl;
  int ret = 0;

  uint8_t *ptr = (uint8_t*)malloc_aligned(MSG_SIZE);
  if (ptr == nullptr) {
    throw std::runtime_error("malloc recv memroy failed.");
  }
  recv_ptr.reset(ptr);

  ptr = nullptr;
  ptr = (uint8_t*)malloc_aligned(MSG_SIZE);
  if (ptr == nullptr) {
    throw std::runtime_error("malloc send memory failed.");
  }
  send_ptr.reset(ptr);

  rpma_mr_local *mr{nullptr};

  ret = rpma_mr_reg(_peer.get(), recv_ptr.get(), MSG_SIZE, RPMA_MR_USAGE_RECV, &mr);
  if (ret) {
    throw std::runtime_error("recv memory region registers failed.");
  }
  recv_mr.reset(mr);

  mr = nullptr;
  ret = rpma_mr_reg(_peer.get(), send_ptr.get(), MSG_SIZE, RPMA_MR_USAGE_SEND, &mr);
  if (ret) {
    throw std::runtime_error("send memory region registers failed.");
  }
  send_mr.reset(mr);

  struct rpma_conn_req *req = nullptr;
  ret = rpma_ep_next_conn_req(ep, nullptr, &req);
  if (ret) {
    throw std::runtime_error("receive an incoming connection request failed.");
  }

  ret = rpma_conn_req_recv(req, recv_mr.get(), 0, MSG_SIZE, recv_ptr.get());
  if (ret) {
    rpma_conn_req_delete(&req);
    throw std::runtime_error("Put an initial receive to be prepared for the first message of the client's ping-pong failed.");
  }

  struct rpma_conn *conn;
  ret = rpma_conn_req_connect(&req, nullptr, &conn);
  if (ret) {
    if (req != nullptr) {
      rpma_conn_req_delete(&req);
    }
    throw std::runtime_error("accept the connection request and obtain the connection object failed.");
  }
  _conn.reset(conn);

  Handle fd;
  ret = rpma_conn_get_event_fd(conn, &fd);
  if (ret) {
    throw std::runtime_error("get the connection's event fd failed");
  }
  _conn_fd.reset(new int(fd));

  ret = rpma_conn_get_completion_fd(conn, &fd);
  if (ret) {
    throw std::runtime_error("get the connection's completion fd failed");
  }
  _comp_fd.reset(new int(fd));
}

int RPMAHandler::register_self() {
  int ret = 0;
  if (auto reactor = _reactor_manager.lock()) {
    if (ret = reactor->register_handler(shared_from_this(), CONNECTION_EVENT)) {
      return ret;
    }
    if (ret = reactor->register_handler(shared_from_this(), COMPLETION_EVENT)) {
      reactor->remove_handler(shared_from_this(), CONNECTION_EVENT);
      return ret;
    }
  }
  return -1;
}

RPMAHandler::~RPMAHandler() {
  std::cout << "I'm in ~RPMAHandler()" << std::endl;
}

Handle RPMAHandler::get_handle(EventType et) const {
  if (et == CONNECTION_EVENT) {
    return *_conn_fd;
  }
  if (et == COMPLETION_EVENT) {
    return *_comp_fd;
  }
  return -1;
}

int RPMAHandler::handle(EventType et) {
  if (et == CONNECTION_EVENT) {
    return handle_connection_event();
  }
  if (et == COMPLETION_EVENT) {
    return handle_completion();
  }
  return -1;
}

int RPMAHandler::handle_connection_event() {
  std::cout << "I'm in RPMAHandler::handle_connection_event()" << std::endl;
  int ret = 0;
  // get next connection's event
  enum rpma_conn_event event;
  ret = rpma_conn_next_event(_conn.get(), &event);
  if (ret) {
    if (ret == RPMA_E_NO_EVENT) {
      return 0;
    } else if (ret == RPMA_E_INVAL) {
      LOG("conn or event is NULL");
    } else if (ret == RPMA_E_UNKNOWN) {
      LOG("unexpected event");
    } else if (ret == RPMA_E_PROVIDER) {
      LOG("rdma_get_cm_event() or rdma_ack_cm_event() failed");
    } else if (ret == RPMA_E_NOMEM) {
      LOG("out of memory");
    }

    rpma_conn_disconnect(_conn.get());
    return ret;
  }

  /* proceed to the callback specific to the received event */
  if (event == RPMA_CONN_ESTABLISHED) {
    //don't do anythings if no private data
    LOG("RPMA_CONN_ESTABLISHED");
    return 0;
  } else if (event == RPMA_CONN_CLOSED) {
    LOG("RPMA_CONN_CLOSED");
  } else if (event == RPMA_CONN_LOST) {
    LOG("RPMA_CONN_LOST");
  } else {
    //RPMA_CONN_UNDEFINED
    LOG("RPMA_CONN_UNDEFINED");
  }

  if (auto reactor = _reactor_manager.lock()) {
    ret = reactor->remove_handler(shared_from_this(), CONNECTION_EVENT);
    ret |= reactor->remove_handler(shared_from_this(), COMPLETION_EVENT);
  }
  return ret;
}


int RPMAHandler::handle_completion() {
  std::cout << "I'm in RPMAHandler::handle_completion()" << std::endl;
  int ret = 0;

  /* prepare detected completions for processing */
  ret = rpma_conn_completion_wait(_conn.get());
  if (ret) {
    /* no completion is ready - continue */
    if (ret == RPMA_E_NO_COMPLETION) {
      return 0;
    } else if (ret == RPMA_E_INVAL) {
      LOG("conn is NULL: %s", rpma_err_2str(ret));
    } else if (ret == RPMA_E_PROVIDER) {
      LOG("ibv_poll_cq(3) failed with a provider error: %s", rpma_err_2str(ret));
    }
    
    /* another error occured - disconnect */
    rpma_conn_disconnect(_conn.get());// TODO: what is problem after twice disconnect?
    return ret;
  }

  /* get next completion */
  struct rpma_completion cmpl;
  ret = rpma_conn_completion_get(_conn.get(), &cmpl);
  if (ret) {
    /* no completion is ready - continue */
    if (ret == RPMA_E_NO_COMPLETION) {
      return 0;
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
    rpma_conn_disconnect(_conn.get());
    return ret;
  }

  /* validate received completion */
  if (cmpl.op_status != IBV_WC_SUCCESS) {
    (void) LOG("[op: %d] received completion is not as expected (%d != %d)\n",
               cmpl.op,
               cmpl.op_status,
               IBV_WC_SUCCESS);

    return ret;
  }

  if (cmpl.op == RPMA_OP_RECV) {
    //TODO: solve the recv condition
    //1. solve the information based on receive
    //2. prepare a receive for the client's response;
    //3. [optional] response the client
    if (cmpl.op_context != recv_ptr.get() || cmpl.byte_len != MSG_SIZE) {
      (void) LOG("received completion is not as expected (%p != %p [cmpl.op_context] || %"
                 PRIu32
                 " != %d [cmpl.byte_len] )",
                 cmpl.op_context, recv_ptr.get(),
                 cmpl.byte_len, MSG_SIZE);
      return ret;
    }
    LOG("RPMA_OP_RECV");
    //deal_require(clnt);
    void (*func)();
    func = []{std::cout << "I'm in lambda" << std::endl;};
    rpma_send(_conn.get(), send_mr.get(), 0, MSG_SIZE,RPMA_F_COMPLETION_ALWAYS, (void*)func);
  } else if ( cmpl.op == RPMA_OP_SEND) {
    LOG("RPMA_OP_SEND");
    void (*func)();
    func = (void (*)())(cmpl.op_context);
    func();
    //TODO: solve the send condition after send successfully
    //now, don't do any thing
  } else {
    LOG("operation: %d\n. Shouldn't step in this", cmpl.op);
  }                                                                                              

  return ret;
}
