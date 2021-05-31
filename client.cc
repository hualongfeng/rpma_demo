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
#include <thread>
#include "common-conn.h"
#include "log.h"

#include "Reactor.h"
#include "EventHandler.h"
#include "EventOp.h"

// void client_init(char *addr, char port) {
//   try {
//     std::shared_ptr<Reactor> reactor = std::make_shared<Reactor>();
//     std::shared_ptr<ClientHandler> rpma_client = std::make_shared<ClientHandler>(addr, port, reactor);
//     int ret = 0;
//     if (ret = rpma_client->register_self()) {
//       return ret;
//     }
//     while(true)
//       reactor->handle_events();
//   } catch (std::runtime_error e) {
//     std::cout << __FILE__ << ":" << __LINE__ << " Runtime error: " << e.what() << std::endl;
//   }
// }

int main(int argc, char* argv[]) {

  /* configure logging thresholds to see more details */
  rpma_log_set_threshold(RPMA_LOG_THRESHOLD, RPMA_LOG_LEVEL_INFO);
  rpma_log_set_threshold(RPMA_LOG_THRESHOLD_AUX, RPMA_LOG_LEVEL_INFO);

  char *addr = argv[1];
  char *port = argv[2];
  std::shared_ptr<Reactor> reactor;// = std::make_shared<Reactor>();
  std::shared_ptr<ClientHandler> rpma_client;// = std::make_shared<ClientHandler>(addr, port, reactor);

  // rpma_client->register_self();
  try {
    reactor = std::make_shared<Reactor>();
    rpma_client = std::make_shared<ClientHandler>(addr, port, reactor);
    int ret = 0;
    if (ret = rpma_client->register_self()) {
      std::cout << "ret :" << ret << std::endl;
      return ret;
    }
  } catch (std::runtime_error e) {
    std::cout << __FILE__ << ":" << __LINE__ << " Runtime error: " << e.what() << std::endl;
  }
// reactor->handle_events();
  // std::cout << "register: " << rpma_client->register_self() << std::endl;
  std::thread th1([reactor]{
      while (true) {
        reactor->handle_events();
        if (reactor->empty()) {
          std::cout << "My event_table is empty!!!" << std::endl;
          break;
        }
      }
  });

  rpma_client->wait_established();
  // // char *addr = nullptr;
  // // char *port = nullptr;
  // // Initialize RPMA server endpoint and register with
  // // the RPMA_Reactor.

  // // NOT REACHED
  // while (true);
  th1.join();
  return 0;
}