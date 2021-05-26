#ifndef _EVENT_HANDLER_H_
#define _EVENT_HANDLER_H_

#include <memory>
#include <iostream>
#include <unistd.h>
#include <librpma.h>
// The type of a handle is system specific
// this example uses RPMA I/O handles, which are 
// plain integer values.
typedef int Handle;

enum EventType {
  ACCEPT_EVENT     = 1u < 0,
  CONNECTION_EVENT = 1u < 1,
  COMPLETION_EVENT = 1u < 2,
};

class EventHandler : public std::enable_shared_from_this<EventHandler> {
public:

  virtual ~EventHandler() {}

  // Hook method that is called back by the RPMA_Reactor
  // to handle events.
  virtual int handle(EventType et) = 0;

  // Hook method that returns the underlying I/O handle.
  virtual Handle get_handle(EventType et) const = 0;

};

using EventHandlerPtr = std::shared_ptr<EventHandler>;

struct EventHandle {
  EventType type;
  EventHandlerPtr handler;
  EventHandle(EventHandlerPtr h, EventType t) : type(t), handler(h) {}
};




#endif //_EVENT_HANDLER_H_