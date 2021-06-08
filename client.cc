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
#include "Types.h"


int main(int argc, char* argv[]) {

  /* configure logging thresholds to see more details */
  rpma_log_set_threshold(RPMA_LOG_THRESHOLD, RPMA_LOG_LEVEL_INFO);
  rpma_log_set_threshold(RPMA_LOG_THRESHOLD_AUX, RPMA_LOG_LEVEL_INFO);

  char *addr = argv[1];
  char *port = argv[2];
  std::string basename("temporary");
  std::shared_ptr<Reactor> reactor;
  std::shared_ptr<ClientHandler> rpma_client;

  int ret = 0;

  try {
    reactor = std::make_shared<Reactor>();
    rpma_client = std::make_shared<ClientHandler>(addr, port, basename, REQUIRE_SIZE,reactor);
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

  rpma_client->prepare_for_send();
  rpma_client->send(nullptr);
  rpma_client->recv([&rpma_client]{
    rpma_client->get_remote_descriptor();
  });

  // // NOT REACHED
  // while (true);
  th1.join();
  return 0;
}
