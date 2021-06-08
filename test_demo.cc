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

// #include "Reactor.h"
// #include "EventHandler.h"
// #include "EventOp.h"
#include "Types.h"


int main(int argc, char* argv[]) {

  RwlCacheInfo info;
  info.cache_id = 123;
  info.cache_size = 1024 * 1024;
  info.pool_name = "rbd";
  info.image_name = "test";
  RpmaConfigDescriptor desc;
  desc.mr_desc_size = 10;
  desc.pcfg_desc_size = 1;
  desc.descriptors = "abcdef";
  bufferlist bl;
  bl.append(bufferptr(4096));
  std::cout << bl.is_page_aligned() << std::endl;
  std::cout << bl.rebuild_page_aligned() << std::endl;
  std::cout << bl.is_page_aligned() << std::endl;
  std::cout << bl.length() << std::endl;
  // std::cout << bl.c_str() << std::endl;
  printf("%p\n", bl.c_str());
  std::cout << bl.is_page_aligned() << std::endl;
  std::cout << bl.is_contiguous() << std::endl;
  // auto node = bl.front();
  // node.set_offset(0);
  // std::cout << bl.length() << std::endl;
  // node.set_offset(0);
  // std::cout << bl.length() << std::endl;
  // return 0;
  bufferlist bl1;
  info.encode(bl1);
  desc.encode(bl1);
  memcpy(bl.c_str(), bl1.c_str(), bl1.length());
  RwlCacheInfo info2;
  RpmaConfigDescriptor desc2;
  auto it = bl.cbegin();
  info2.decode(it);
  desc2.decode(it);
  std::cout << info.cache_id << " " << info.cache_size << " " << info.pool_name << " " << info.image_name << std::endl;
  std::cout << info2.cache_id << " " << info2.cache_size << " " << info2.pool_name << " " << info2.image_name << std::endl;
  std::cout << desc.mr_desc_size << " " << desc.pcfg_desc_size << " " << desc.descriptors << std::endl;
  std::cout << desc2.mr_desc_size << " " << desc2.pcfg_desc_size << " " << desc2.descriptors << std::endl;

  printf("%p\n", bl.c_str());
  // bufferlist bl;
  // bl.append(bufferptr(4096));
  std::cout << bl.is_page_aligned() << std::endl;
  std::cout << bl.rebuild_page_aligned() << std::endl;
  std::cout << bl.is_page_aligned() << std::endl;
  std::cout << bl.length() << std::endl;
  // std::cout << bl.c_str() << std::endl;
  printf("%p\n", bl.c_str());
  std::cout << bl.is_page_aligned() << std::endl;

  return 0;

}
