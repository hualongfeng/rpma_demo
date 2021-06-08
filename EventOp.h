#ifndef _EVENT_OP_H_
#define _EVENT_OP_H_

#include "EventHandler.h"
#include "Reactor.h"
#include <librpma.h>
#include <unistd.h>
#include <inttypes.h>
#include <atomic>
#include <set>
#include "MemoryManager.h"
#include "RpmaOp.h"
#include "Types.h"
#include <rados/librados.hpp>

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

  // Get the I/O Handle (called by the RPMA_Reactor when 
  // RPMA_Handler is registered).
  virtual Handle get_handle(EventType et) const override;
private:
  int register_mr_to_descriptor(enum rpma_op op);
  int register_cfg_to_descriptor();
  int get_descriptor_for_write();
  int get_descriptor_for_flush();
  int get_descriptor();
  void deal_require();

  std::set<RpmaOp*> callback_table;
  // memory resource
  MemoryManager data_manager;
  unique_rpma_mr_ptr data_mr;
  // unique_malloc_ptr send_ptr;
  bufferlist send_bl;
  unique_rpma_mr_ptr send_mr;
  // unique_malloc_ptr recv_ptr;
  bufferlist recv_bl;
  unique_rpma_mr_ptr recv_mr;

  // Receives connection request from a client
  unique_handle_ptr _conn_fd;
  unique_handle_ptr _comp_fd;
  std::shared_ptr<struct rpma_peer> _peer;
  unique_rpma_conn_ptr _conn;

};

class ClientHandler : public EventHandlerInterface, public std::enable_shared_from_this<ClientHandler> {
public:
  // Initialize the client request
  ClientHandler(const std::string& addr,
                const std::string& port,
                const std::string& basename,
                const size_t image_size,
                const std::weak_ptr<Reactor> reactor_manager);

  ~ClientHandler();
  virtual int register_self() override;
  // Hook method that handles the connection request from clients.
  virtual int handle(EventType et) override;

  int handle_completion();
  int handle_connection_event();

  // Get the I/O Handle (called by the RPMA_Reactor when
  // RPMA_Handler is registered).
  virtual Handle get_handle(EventType et) const override;

  // wait for the connection to establish
  void wait_established() {
    std::cout << "I'm in wait_established()" << std::endl;
    while (connected.load() != true);
  }
  void close();
  int send(std::function<void()> callback);
  int recv(std::function<void()> callback);
  int write(void *src,
            size_t offset,
            size_t len,
            std::function<void()> callback);
  int flush(size_t offset,
            size_t len,
            std::function<void()> callback);
  int write_atomic(std::function<void()> callback);
  int get_remote_descriptor();
  int prepare_for_send();
private:
  enum rpma_flush_type _flush_type;
  std::string _address;
  std::string _port;
  // memory resource
  MemoryManager data_manager;
  unique_rpma_mr_ptr data_mr;
  size_t _image_size;
  struct rpma_mr_remote* _image_mr;
  std::string _basename;
  std::set<RpmaOp*> callback_table;

  // unique_malloc_ptr send_ptr;
  bufferlist send_bl;
  unique_rpma_mr_ptr send_mr;
  // unique_malloc_ptr recv_ptr;
  bufferlist recv_bl;
  unique_rpma_mr_ptr recv_mr;

  // Receives connection request from a client
  unique_handle_ptr _conn_fd;
  unique_handle_ptr _comp_fd;
  unique_rpma_peer_ptr _peer;
  unique_rpma_conn_ptr _conn;

  std::atomic<bool> connected{false};
};
#endif //_EVENT_OP_H_
