#include "Reactor.h"

#include <inttypes.h>
#include <librpma.h>
#include <iostream>
//#include <cstdlib>
//#include <cstdio>
#include <assert.h>
#include <sys/epoll.h>
#include <unistd.h>
#include <libpmem.h>
#include <string>
#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>
#include <stddef.h>
#include <sys/epoll.h>
#include <fcntl.h>
#include <memory>
#include <unordered_map>
#include "common-conn.h"
#include "log.h"

Reactor::Reactor() {
  _epoll = epoll_create1(EPOLL_CLOEXEC);
  if (_epoll == -1) {
    throw std::runtime_error("epoll_create1 failed\n");
  }
}

Reactor::~Reactor() {
  close(_epoll);
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

int Reactor::register_handler(EventHandlerPtr eh, EventType et) {
  Handle fd = eh->get_handle(et);
  if (fd == -1) {
    return -1;
  }

  int ret = fd_set_nonblock(fd);
  if (ret) {
    return -1;
  }

  //event_table.emplace(fd, EventHandle{eh, et}); 
  //event_table.emplace(fd, EventHandle{eh, et}); 

  // prepare an epoll event
  struct epoll_event event;
  event.events = EPOLLIN;
  //event.data.ptr = &(event_table[fd]);

  if (epoll_ctl(_epoll, EPOLL_CTL_ADD, eh->get_handle(et), &event)) {
    int err = errno;
    //event_table.erase(fd);
    return err;
  }

  return 0;
}

int Reactor::remove_handler(EventHandlerPtr eh, EventType et) {
  Handle fd = eh->get_handle(et);
  if (fd == -1) {
    return -1;
  }

  epoll_ctl(_epoll, EPOLL_CTL_DEL, fd, NULL);
  //event_table.erase(fd);
  return 0;
}


//int Reactor::handle_events(TimeValue *timeout = 0) {
int Reactor::handle_events() {
  int ret = 0;
  /* process epoll's events */
  struct epoll_event event;
  EventHandle *event_handle;
  while ((ret = epoll_wait(_epoll, &event, 1 /* # of events */,
                              TIMEOUT_1500S)) == 1) {
    event_handle = static_cast<EventHandle*>(event.data.ptr);
    event_handle->handler->handle(event_handle->type);
  }
  return ret;
}