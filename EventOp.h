#ifndef _EVENT_OP_H_
#define _EVENT_OP_H_

#include "EventHandler.h"
#include "Reactor.h"

class EventHandlerInterface : public EventHandler {
public:
    EventHandlerInterface(std::shared_ptr<Reactor> reactor_ptr): _reactor_manager(reactor_ptr) {}
protected:
    std::shared_ptr<Reactor> _reactor_manager;
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
    rpma_peer_delete(&peer);
    std::cout << "I'm in RpmaPeerDeleter()" << std::endl;
  }
};

struct RpmaEpDeleter {
  void operator() (struct rpma_ep *ep) {
    rpma_ep_shutdown(&ep);
  }
};
using unique_rpma_ep_ptr = std::unique_ptr<struct rpma_ep, RpmaEpDeleter>;

// Handles client connection requests.
class AcceptorHandler : public EventHandlerInterface {
public:
  // Initialize the acceptor endpoint and register
  // with the Initiation Dispatcher
  AcceptorHandler(const std::string& addr,
                  const std::string& port,
                  const std::shared_ptr<Reactor> reactor_manager);

  ~AcceptorHandler();

  std::shared_ptr<AcceptorHandler> shared_from_this() {
    return std::static_pointer_cast<AcceptorHandler>(EventHandler::shared_from_this());
  }

  // Factory method that accepts a new connection request and
  // creates a RPMA_Handler object to handle connection event
  // using the connection.
  virtual int handle(EventType et);

  // Get the I/O  Handle (called by Initiation Dispatcher when
  // Logging Acceptor is registered).
  Handle get_handle(EventType et) const;

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
    rpma_conn_disconnect(conn); // TODO: how to avoid twice disconnect?
    rpma_conn_delete(&conn);
  }
};
using unique_rpma_conn_ptr = std::unique_ptr<struct rpma_conn, RpmaConnDeleter>;

struct RpmaMRDeleter {
  void operator() (struct rpma_mr_local *mr_ptr) {
    rpma_mr_dereg(&mr_ptr);
  }
};
using unique_rpma_mr_ptr = std::unique_ptr<struct rpma_mr_local, RpmaMRDeleter>;


class RPMAHandler : public EventHandlerInterface {
public:
  // Initialize the client request
  RPMAHandler(std::shared_ptr<struct rpma_peer> peer,
              struct rpma_ep *ep,
              const std::shared_ptr<Reactor> reactor_manager);
  ~RPMAHandler();

  // Hook method that handles the connection request from clients.
  virtual int handle(EventType et);

  int handle_completion();
  int handle_connection_event();

  std::shared_ptr<RPMAHandler> shared_from_this() {
    return std::static_pointer_cast<RPMAHandler>(EventHandler::shared_from_this());
  }

  // Get the I/O Handle (called by the RPMA_Reactor when 
  // RPMA_Handler is registered).
  Handle get_handle(EventType et) const;
private:
  std::unique_ptr<char> ptr;
  unique_rpma_mr_ptr mr;
  std::unique_ptr<char> send_ptr;
  unique_rpma_mr_ptr send_mr;
  std::unique_ptr<char> recv_ptr;
  unique_rpma_mr_ptr recv_mr;

  // Receives connection request from a client

  unique_handle_ptr _conn_fd;
  unique_handle_ptr _comp_fd;
  std::shared_ptr<struct rpma_peer> _peer;
  unique_rpma_conn_ptr _conn;

};

#endif //_EVENT_OP_H_