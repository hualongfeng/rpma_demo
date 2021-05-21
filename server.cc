#include <inttypes.h>
#include <librpma.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/epoll.h>
#include <unistd.h>
#include <libpmem.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <erron.h>
#include <stddef.h>
#include <sys/epoll.h>

namespace rpma {
enum Event_Type {
  ACCEPT_EVENT     = 1u < 0,
  CONNECTION_EVENT = 1u < 1,
  COMPLETION_EVENT = 1u < 2,
};
// The type of a handle is system specific
// this example uses RPMA I/O handles, which are 
// plain integer values.
typedef int Handle;
class EventHandler;
//RPMA_Reactor
class Reactor {
public:
  Reactor();
  ~Reactor();

  // Register an Event_Handler of a particular Event_Type.
  int register_handler(Event_Handler *eh, Event_Type et);

  // Remove an Event_Handler of a particular Event_Type.
  int remove_handler(Event_Handler *eh, Event_Type et);

  // Entry point into the reactive event loop.
  int handle_events(Time_Value *timeout = 0);

private:
  int fd_set_nonblock(int fd);

  int _epoll;

};

Reactor::Reactor() {
  epoll = epoll_create1(EPOLL_CLOEXEC);
}

int Reactor::fd_set_nonblock(int fd) {
  int ret = fcntl(fd, F_GETFL);
  if (ret < 0)
    return errno;

  int flags = ret | O_NONBLOCK;
  ret = fcntl(fd, F_SETFL, flags);
  if (ret < 0)
    return errno;

  return 0;
}


class EventHandler {
public:

  EventHandler(std::shared_ptr<Reactor> reactor_ptr): reactor_manager(reactor_ptr) {}

  // Hook method that is called back by the RPMA_Reactor
  // to handle events.
  virtual int handle(Event_Type et) = 0;

  // Hook method that returns the underlying I/O handle.
  virtual Handle get_handle(void) const = 0;

protected:
  std::shared_ptr<Reactor> _reactor_manager;
  Handle fd;
}

using EventHandlerPtr = std::shared_ptr<EventHandler>;

// Handles client connection requests.
class AcceptorHandler : public EventHandler {
public:
  // Initialize the acceptor endpoint and register
  // with the Initiation Dispatcher
  Acceptor_Handler(const std::string& addr,
                   const std::string& port,
                   const std::shared_ptr<Reactor>& reactor_manager);

  ~AcceptorHandler();

  // Factory method that accepts a new connection request and
  // creates a RPMA_Handler object to handle connection event
  // using the connection.
  virtual void handle_event(Event_Type et);

  // Get the I/O  Handle (called by Initiation Dispatcher when
  // Logging Acceptor is registered).
  virtual Handle get_handle(void) const;

  int init();

private:
  // Socket factory that accepts client connection.
  std::string _address;
  std::string _port;
  struct rpma_peer *peer;
}

AcceptorHandler::Acceptor_Handler(const std::string& addr,
                   const std::string& port,
                   const std::shared_ptr<Reactor>& reactor_manager)
  : EventHandler(reactor_manager), _address(addr), _port(port)
 {

  // Register acceptor with the Initiation Dispatcher,
  // which "double dispatches" the RPMA_Acceptor::get_handle()
  // method to obtain the Handle.
  // RPMA_Reactor::instance()->register_handler(this, ACCEPT_EVENT);
}

int AcceptorHandler::init() {
  if ((ret = server_peer_via_address(addr, &peer))) {
    return ret;
  }
}


~AcceptorHandler::Acceptor_Handler() {

}

void Acceptor_Handler::handle_event(Event_Type et) {
  // Can only be called for an ACCEPT event.
  assert(et == ACCEPT_EVENT);

  // Accept the connection.

  // Create a new RPMA_Handler.
  // registers itself with the RPMA_Reactor
  RPMA_Handler *handler = new RPMA_Handler(new_req);
}

class RPMA_Handler : public Event_Handler {
public:
  // Initialize the client request
  RPMA_Handler(req);

  // Hook method that handles the connection request from clients.
  virtual void handle_event(Event_Type et);

  // Get the I/O Handle (called by the RPMA_Reactor when 
  // RPMA_Handler is registered).
  virtual Handle get_handle(void) const {
    return;
  }
private:
  // Receives connection request from a client
}

RPMA_Handler::RPMA_Handler(req) {
  // Register with the dispatcher for CONNECTION_EVENT.
  RPMA_Reactor::instance()->register_handler(this, CONNECTION_EVENT);
}

void RPMA_Handler::handle_event(Event_Type et) {
  if (et == CONNECTION_EVENT) {
  } else if (et == COMPLETION_EVENT) {
  }
}

int main() {
  char *addr = nullptr;
  char *port = nullptr;
  // Initialize RPMA server endpoint and register with
  // the RPMA_Reactor.
  RPMA_Acceptor rpma_acceptor(addr, port)

  while(true)
    RPMA_Reactor::instance()->handle_events();

  // NOT REACHED
  return 0;
}

} // namespace rpma
