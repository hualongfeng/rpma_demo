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

#include "Reactor.h"
#include "EventHandler.h"
#include "EventOp.h"

int main(int argc, char* argv[]) {
  char *addr = argv[1];
  char *port = argv[2];
  // char *addr = nullptr;
  // char *port = nullptr;
  // Initialize RPMA server endpoint and register with
  // the RPMA_Reactor.
  try {
    std::shared_ptr<Reactor> reactor = std::make_shared<Reactor>();
    AcceptorHandler rpma_acceptor(addr, port, reactor);
    while(true)
      reactor->handle_events();
  } catch (std::runtime_error e) {
    std::cout << __FILE__ << ":" << __LINE__ << " Runtime error: " << e.what() << std::endl;
  }
  // NOT REACHED
  return 0;
}