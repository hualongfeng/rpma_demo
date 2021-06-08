#include "EventHandler.h"
#include "EventOp.h"
#include "MemoryManager.h"
#include "RpmaOp.h"

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
#include "Types.h"
#include <rados/librados.hpp>

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

  rpma_mr_local *mr{nullptr};
  recv_bl.append(bufferptr(MSG_SIZE));
  recv_bl.rebuild_page_aligned();
  ret = rpma_mr_reg(_peer.get(), recv_bl.c_str(), MSG_SIZE, RPMA_MR_USAGE_RECV, &mr);
  if (ret) {
    throw std::runtime_error("recv memory region registers failed.");
  }
  recv_mr.reset(mr);

  mr = nullptr;
  send_bl.append(bufferptr(MSG_SIZE));
  send_bl.rebuild_page_aligned();
  ret = rpma_mr_reg(_peer.get(), send_bl.c_str(), MSG_SIZE, RPMA_MR_USAGE_SEND, &mr);
  if (ret) {
    throw std::runtime_error("send memory region registers failed.");
  }
  send_mr.reset(mr);

  struct rpma_conn_req *req = nullptr;
  ret = rpma_ep_next_conn_req(ep, nullptr, &req);
  if (ret) {
    throw std::runtime_error("receive an incoming connection request failed.");
  }

  /* prepare a receive for the client's response */
  std::unique_ptr<RpmaReqRecv> recv = std::make_unique<RpmaReqRecv>([self=this](){
    self->deal_require();
  });
  ret = (*recv)(req, recv_mr.get(), 0, MSG_SIZE, recv.get());
  if (ret == 0) {
    callback_table.insert(recv.get());
    recv.release();
  }
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
  return ret;
}

RPMAHandler::~RPMAHandler() {
  std::cout << "I'm in ~RPMAHandler()" << std::endl;
  std::cout << "table size: " << callback_table.size() << std::endl;
  for (auto &it : callback_table) {
    std::cout << "pointer: " << it << std::endl;
    auto op_func = std::unique_ptr<RpmaOp>{it};
  }
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
    LOG("RPMA_OP_RECV");
  } else if ( cmpl.op == RPMA_OP_SEND) {
    LOG("RPMA_OP_SEND");
  } else {
    LOG("operation: %d\n. Shouldn't step in this", cmpl.op);
  }

  if (cmpl.op_context != nullptr) {
    auto op_func = std::unique_ptr<RpmaOp>{static_cast<RpmaOp*>(const_cast<void *>(cmpl.op_context))};
    callback_table.erase(op_func.get());
    op_func->do_callback();
  } else {
    LOG("op_context is nullptr");
  }
  return ret;
}

int RPMAHandler::register_mr_to_descriptor(enum rpma_op op) {
  int ret = 0;

  int usage = 0;
  switch (op) {
    case RPMA_OP_FLUSH:
      usage |= (data_manager.is_pmem() ? RPMA_MR_USAGE_FLUSH_TYPE_PERSISTENT : RPMA_MR_USAGE_FLUSH_TYPE_VISIBILITY);
      // don't have break
    case RPMA_OP_WRITE:
      usage |= RPMA_MR_USAGE_WRITE_DST;
      break;
    case RPMA_OP_READ:
      usage |= RPMA_MR_USAGE_READ_SRC;
//  break;
//  don't resolve read operation
    default:
      LOG("Warn: Don't step in this\n");
      break;
  }

  /* register the memory */
  rpma_mr_local *mr{nullptr};
  if ((ret = rpma_mr_reg(_peer.get(), data_manager.get_pointer(), data_manager.size(),
                       usage, &mr))) {
    LOG("%s", rpma_err_2str(ret));
    return ret;
  }
  data_mr.reset(mr);

  /* get size of the memory region's descriptor */
  size_t mr_desc_size;
  ret = rpma_mr_get_descriptor_size(mr, &mr_desc_size);

  /* calculate data for the client write */
  RwlReplicaInitRequestReply init_reply(RWL_REPLICA_INIT_SUCCESSED);
  init_reply.desc.mr_desc_size = mr_desc_size;
  init_reply.desc.descriptors.resize(mr_desc_size);

  /* get the memory region's descriptor */
  rpma_mr_get_descriptor(data_mr.get(), &init_reply.desc.descriptors[0]);

  if (op == RPMA_OP_FLUSH) {
    /* resources - memory region */
    struct rpma_peer_cfg *pcfg = NULL;

    /* create a peer configuration structure */
    ret = rpma_peer_cfg_new(&pcfg);

    if (data_manager.is_pmem()) {
      /* configure peer's direct write to pmem support */
      ret = rpma_peer_cfg_set_direct_write_to_pmem(pcfg, true);
      if (ret) {
        (void) rpma_peer_cfg_delete(&pcfg);
        LOG("rpma_peer_cfg_set_direct_write_to_pmem failed");
        return ret;
      }
    }

    /* get size of the peer config descriptor */
    size_t pcfg_desc_size;
    ret = rpma_peer_cfg_get_descriptor_size(pcfg, &pcfg_desc_size);
    init_reply.desc.pcfg_desc_size = pcfg_desc_size;
    init_reply.desc.descriptors.resize(mr_desc_size + pcfg_desc_size);

    /*
    * Get the peer's configuration descriptor.
    * The pcfg_desc descriptor is saved in the `descriptors[]` array
    * just after the mr_desc descriptor.
    */
    rpma_peer_cfg_get_descriptor(pcfg, &init_reply.desc.descriptors[mr_desc_size]);

    rpma_peer_cfg_delete(&pcfg);
  }
  bufferlist bl;
  init_reply.encode(bl);
  assert(bl.length() < MSG_SIZE);
  memcpy(send_bl.c_str(), bl.c_str(), bl.length());
  return 0;
}

int RPMAHandler::get_descriptor_for_write() {
  RwlReplicaInitRequest init;
  auto it = recv_bl.cbegin();
  init.decode(it);
  LOG("Alloc memory size: %" PRIu64 "\n", init.info.cache_size);

  std::string path("rbd-pwl." + init.info.pool_name + "." + init.info.image_name + ".pool." + std::to_string(init.info.cache_id));
  if (data_manager.get_pointer() == nullptr) {
    data_manager.init(init.info.cache_size, path);
  }
  return register_mr_to_descriptor(RPMA_OP_FLUSH);
}

int RPMAHandler::get_descriptor() {
  RwlReplicaRequest request;
  auto it = recv_bl.cbegin();
  request.decode(it);
  switch (request.type) {
    case RWL_REPLICA_INIT_REQUEST:
      return get_descriptor_for_write();
    case RWL_REPLICA_FINISHED_REQUEST:
    default:
      LOG("the op:%d isn't supported now", request.type);
  }
  return -1;
}

void RPMAHandler::deal_require() {
  get_descriptor();

  /* prepare a receive for the client's response */
  std::unique_ptr<RpmaRecv> rec = std::make_unique<RpmaRecv>([self=this](){
    self->deal_require();
  });
  int ret = (*rec)(_conn.get(), recv_mr.get(), 0, MSG_SIZE, rec.get());
  if (ret == 0) {
    callback_table.insert(rec.get());
    rec.release();
  }

  /* send the common_data to the client */
  rpma_send(_conn.get(), send_mr.get(), 0, MSG_SIZE,RPMA_F_COMPLETION_ALWAYS, nullptr);
}


ClientHandler::ClientHandler(const std::string& addr,
                             const std::string& port,
                             const std::string& basename,
                             const size_t image_size,
                             const std::weak_ptr<Reactor> reactor_manager)
    : EventHandlerInterface(reactor_manager), _address(addr), _port(port),
    _basename(basename), _image_size(image_size) {
  std::cout << "I'm in ClientHandler::ClientHandler()" << std::endl;
  int ret = 0;
  struct rpma_peer *peer = nullptr;
  ret = client_peer_via_address(addr.c_str(), &peer);
  if (ret) {
    throw std::runtime_error("lookup an ibv_context via the address and create a new peer using it failed");
  }
  _peer.reset(peer);

  rpma_mr_local *mr{nullptr};

  recv_bl.append(bufferptr(MSG_SIZE));
  recv_bl.rebuild_page_aligned();
  ret = rpma_mr_reg(peer, recv_bl.c_str(), MSG_SIZE, RPMA_MR_USAGE_RECV, &mr);
  if (ret) {
    throw std::runtime_error("recv memory region registers failed.");
  }
  recv_mr.reset(mr);

  mr = nullptr;
  send_bl.append(bufferptr(MSG_SIZE));
  send_bl.rebuild_page_aligned();
  ret = rpma_mr_reg(peer, send_bl.c_str(), MSG_SIZE, RPMA_MR_USAGE_SEND, &mr);
  if (ret) {
    throw std::runtime_error("send memory region registers failed.");
  }
  send_mr.reset(mr);

  struct rpma_conn_req *req = nullptr;
  struct rpma_conn_cfg *cfg_ptr = nullptr;
  ret = rpma_conn_cfg_new(&cfg_ptr);
  if (ret) {
    throw std::runtime_error("new cfg failed");
  }

  //TODO: make those config
  rpma_conn_cfg_set_sq_size(cfg_ptr, 200);
  rpma_conn_cfg_set_rq_size(cfg_ptr, 200);
  rpma_conn_cfg_set_cq_size(cfg_ptr, 200);

  ret = rpma_conn_req_new(peer, addr.c_str(), port.c_str(), cfg_ptr, &req);
  rpma_conn_cfg_delete(&cfg_ptr);
  if (ret) {
    throw std::runtime_error("create a new outgoing connection request object failed");
  }

  struct rpma_conn *conn;
  ret = rpma_conn_req_connect(&req, nullptr, &conn);
  if (ret) {
    if (req != nullptr) {
      rpma_conn_req_delete(&req);
    }
    throw std::runtime_error("initiate processing the connection request");
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

ClientHandler::~ClientHandler() {
  std::cout << "I'm in ClientHandler::~ClientHandler()" << std::endl;
  std::cout << "table size: " << callback_table.size() << std::endl;
  for (auto &it : callback_table) {
    std::cout << "pointer: " << it << std::endl;
    auto op_func = std::unique_ptr<RpmaOp>{it};
  }
}

int ClientHandler::register_self() {
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
  return ret;
}

// Hook method that handles the connection request from clients.
int ClientHandler::handle(EventType et) {
  if (et == CONNECTION_EVENT) {
    return handle_connection_event();
  }
  if (et == COMPLETION_EVENT) {
    return handle_completion();
  }
  return -1;
}

int ClientHandler::handle_completion() {
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
    LOG("RPMA_OP_RECV");
  } else if ( cmpl.op == RPMA_OP_SEND) {
    LOG("RPMA_OP_SEND");
  } else {
    LOG("operation: %d\n. Shouldn't step in this", cmpl.op);
  }

  if (cmpl.op_context != nullptr) {
    auto op_func = std::unique_ptr<RpmaOp>{static_cast<RpmaOp*>(const_cast<void *>(cmpl.op_context))};
    callback_table.erase(op_func.get());
    op_func->do_callback();
  } else {
    LOG("op_context is nullptr");
  }
  return ret;
}

int ClientHandler::handle_connection_event() {
  std::cout << "I'm in ClientHandler::handle_connection_event()" << std::endl;
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
    connected = true;
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

// Get the I/O Handle (called by the RPMA_Reactor when
// RPMA_Handler is registered).
Handle ClientHandler::get_handle(EventType et) const{
  if (et == CONNECTION_EVENT) {
    return *_conn_fd;
  }
  if (et == COMPLETION_EVENT) {
    return *_comp_fd;
  }
  return -1;
}

int ClientHandler::get_remote_descriptor() {
  RwlReplicaInitRequestReply init_reply;
  auto it = recv_bl.cbegin();
  init_reply.decode(it);
  int ret = 0;
  LOG("init_reply.type: %d", init_reply.type);
  if (init_reply.type == RWL_REPLICA_INIT_SUCCESSED) {
    struct RpmaConfigDescriptor *dst_data = &(init_reply.desc);
    // Create a remote peer configuration structure from the received
    // descriptor and apply it to the current connection
    bool direct_write_to_pmem = false;
    struct rpma_peer_cfg *pcfg = nullptr;
    if (dst_data->pcfg_desc_size) {
      rpma_peer_cfg_from_descriptor(&dst_data->descriptors[dst_data->mr_desc_size], dst_data->pcfg_desc_size, &pcfg);
      rpma_peer_cfg_get_direct_write_to_pmem(pcfg, &direct_write_to_pmem);
      rpma_conn_apply_remote_peer_cfg(_conn.get(), pcfg);
      rpma_peer_cfg_delete(&pcfg);
      // TODO: error handle
    }

    // Create a remote memory registration structure from received descriptor
    if (ret = rpma_mr_remote_from_descriptor(&dst_data->descriptors[0], dst_data->mr_desc_size, &_image_mr)) {
      LOG("%s", rpma_err_2str(ret));
    }

    //get the remote memory region size
    size_t size;
    if (ret = rpma_mr_remote_get_size(_image_mr, &size)) {
      LOG("%s", rpma_err_2str(ret));
    }

    if (size < _image_size) {
      LOG("%s:%d: Remote memory region size too small for writing the"
      " data of the assumed size (%zu < %ld)",
      __FILE__, __LINE__, size, _image_size);
      return -1;
    }
    /* determine the flush type */
    if (direct_write_to_pmem) {
      LOG("RPMA_FLUSH_TYPE_PERSISTENT is supported");
      _flush_type = RPMA_FLUSH_TYPE_PERSISTENT;
    } else {
      LOG("RPMA_FLUSH_TYPE_PERSISTENT is NOT supported");
      _flush_type = RPMA_FLUSH_TYPE_VISIBILITY;
    }
  }
  return ret;
}

int ClientHandler::prepare_for_send() {
  RwlReplicaInitRequest init(RWL_REPLICA_INIT_REQUEST);
  init.info.cache_id = 1;
  init.info.cache_size = _image_size;
  init.info.pool_name = "rbd";
  init.info.image_name = "test";
  bufferlist bl;
  init.encode(bl);
  assert(bl.length() < MSG_SIZE);
  memcpy(send_bl.c_str(), bl.c_str(), bl.length());
  return 0;
}

int ClientHandler::send(std::function<void()> callback) {
  int ret = 0;
  std::unique_ptr<RpmaSend> usend = std::make_unique<RpmaSend>(callback);
  ret = (*usend)(_conn.get(), send_mr.get(), 0, MSG_SIZE,RPMA_F_COMPLETION_ALWAYS, usend.get());
  if (ret == 0) {
    callback_table.insert(usend.get());
    usend.release();
  }
  return ret;
}

int ClientHandler::recv(std::function<void()> callback) {
  int ret = 0;
  std::unique_ptr<RpmaRecv> rec = std::make_unique<RpmaRecv>(callback);
  ret = (*rec)(_conn.get(), recv_mr.get(), 0, MSG_SIZE, rec.get());
  if (ret == 0) {
    callback_table.insert(rec.get());
    rec.release();
  }
  return ret;
}

int ClientHandler::write(void *src,
                         size_t offset,
                         size_t len,
                         std::function<void()> callback) {
  int ret = 0;
  std::unique_ptr<RpmaWrite> uwrite = std::make_unique<RpmaWrite>(callback);

  rpma_mr_local *mr{nullptr};

  ret = rpma_mr_reg(_peer.get(), src, offset + len, RPMA_MR_USAGE_WRITE_SRC, &mr);
  if (ret) {
    return ret;
  }
  uwrite->reset(mr);

  ret = (*uwrite)(_conn.get(), _image_mr, offset, mr, offset, len, RPMA_F_COMPLETION_ALWAYS, uwrite.get());
  if (ret == 0) {
    callback_table.insert(uwrite.get());
    uwrite.release();
  }
  ceph::buffer::list bl;
  return ret;
}

int ClientHandler::flush(size_t offset,
                         size_t len,
                         std::function<void()> callback) {
  int ret = 0;
  std::unique_ptr<RpmaFlush> uflush = std::make_unique<RpmaFlush>(callback);
  ret = (*uflush)(_conn.get(), _image_mr, offset, len, _flush_type, RPMA_F_COMPLETION_ALWAYS, uflush.get());
  if (ret == 0) {
    callback_table.insert(uflush.get());
    uflush.release();
  }
  return ret;
}