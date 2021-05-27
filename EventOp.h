#ifndef _EVENT_OP_H_
#define _EVENT_OP_H_

#include "EventHandler.h"
#include "Reactor.h"

class EventHandlerInterface : public EventHandler {
public:
  EventHandlerInterface(std::weak_ptr<Reactor> reactor_ptr): _reactor_manager(reactor_ptr) {}
  ~EventHandlerInterface() {
    std::cout << "I'm in EventHandlerInterface::~EventHandlerInterface()" << std::endl;
  }
protected:
  std::weak_ptr<Reactor> _reactor_manager;
};


struct HandleDeleter {
  void operator() (Handle *ptr) {
    close(*ptr);
    delete ptr;
  }
};
using unique_handle_ptr = std::unique_ptr<Handle, HandleDeleter>;

struct RpmaPeerDeleter {
  void operator() (struct rpma_peer *peer) {
    std::cout << "I'm in RpmaPeerDeleter()" << std::endl;
    rpma_peer_delete(&peer);
  }
};

struct RpmaEpDeleter {
  void operator() (struct rpma_ep *ep) {
    std::cout << "I'm in RpmaEpDeleter()" << std::endl;
    rpma_ep_shutdown(&ep);
  }
};
using unique_rpma_ep_ptr = std::unique_ptr<struct rpma_ep, RpmaEpDeleter>;

// Handles client connection requests.
class AcceptorHandler : public EventHandlerInterface, public std::enable_shared_from_this<AcceptorHandler> {
public:
  // Initialize the acceptor endpoint and register
  // with the Initiation Dispatcher
  AcceptorHandler(const std::string& addr,
                  const std::string& port,
                  const std::weak_ptr<Reactor> reactor_manager);

  ~AcceptorHandler();

  virtual int register_self() override;

//   std::shared_ptr<AcceptorHandler> shared_from_this() {
//     return std::static_pointer_cast<AcceptorHandler>(EventHandler::shared_from_this());
//   }

  // Factory method that accepts a new connection request and
  // creates a RPMA_Handler object to handle connection event
  // using the connection.
  virtual int handle(EventType et) override;

  // Get the I/O  Handle (called by Initiation Dispatcher when
  // Logging Acceptor is registered).
  virtual Handle get_handle(EventType et) const override;

private:
  // Socket factory that accepts client connection.
  unique_handle_ptr _fd;
  std::string _address;
  std::string _port;
  std::shared_ptr<struct rpma_peer> _peer;
  unique_rpma_ep_ptr _ep;
};


struct RpmaConnDeleter {
  void operator() (struct rpma_conn *conn) {
    std::cout << "I'm in RpmaConnDeleter()" << std::endl;
    rpma_conn_disconnect(conn); // TODO: how to avoid twice disconnect?
    rpma_conn_delete(&conn);
  }
};
using unique_rpma_conn_ptr = std::unique_ptr<struct rpma_conn, RpmaConnDeleter>;

struct RpmaMRDeleter {
  void operator() (struct rpma_mr_local *mr_ptr) {
    std::cout << "I'm in RpmaMRDeleter()" << std::endl;
    rpma_mr_dereg(&mr_ptr);
  }
};
using unique_rpma_mr_ptr = std::unique_ptr<struct rpma_mr_local, RpmaMRDeleter>;

struct MallocDeleter {
  void operator() (uint8_t *ptr) {
    std::cout << "I'm in MallocAlignedDeleter()" << std::endl;
    free(ptr);
  }
};
using unique_malloc_ptr = std::unique_ptr<uint8_t, MallocDeleter>;

class RPMAHandler : public EventHandlerInterface, public std::enable_shared_from_this<RPMAHandler>{
public:
  // Initialize the client request
  RPMAHandler(std::shared_ptr<struct rpma_peer> peer,
              struct rpma_ep *ep,
              const std::weak_ptr<Reactor> reactor_manager);
  ~RPMAHandler();
  virtual int register_self() override;
  // Hook method that handles the connection request from clients.
  virtual int handle(EventType et) override;

  int handle_completion();
  int handle_connection_event();

//   std::shared_ptr<RPMAHandler> shared_from_this() {
//     return std::static_pointer_cast<RPMAHandler>(EventHandler::shared_from_this());
//   }

  // Get the I/O Handle (called by the RPMA_Reactor when 
  // RPMA_Handler is registered).
  virtual Handle get_handle(EventType et) const override;
private:
  std::unique_ptr<uint8_t> ptr;
  unique_rpma_mr_ptr mr;
  unique_malloc_ptr send_ptr;
  unique_rpma_mr_ptr send_mr;
  unique_malloc_ptr recv_ptr;
  unique_rpma_mr_ptr recv_mr;

  // Receives connection request from a client

  unique_handle_ptr _conn_fd;
  unique_handle_ptr _comp_fd;
  std::shared_ptr<struct rpma_peer> _peer;
  unique_rpma_conn_ptr _conn;

};

#endif //_EVENT_OP_H_