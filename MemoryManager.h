//
// Created by fenghl on 2021/5/29.
//

#ifndef RPMA_DEMO_MEMORYMANAGER_H
#define RPMA_DEMO_MEMORYMANAGER_H

#include <string>
#include <inttypes.h>

class MemoryManager {
public:
  MemoryManager(uint64_t size, std::string &path);
  MemoryManager() {}
  ~MemoryManager();
  void init(uint64_t size, std::string &path);
  void *get_pointer();
  uint64_t size() {return _size;}
  bool is_pmem() { return _is_pmem;}
private:
  void *get_memory_from_pmem(std::string &path);
  void *get_memory_from_dram();

  void *_data{nullptr};
  uint64_t _size;
  bool _is_pmem{false};
};


#endif //RPMA_DEMO_MEMORYMANAGER_H
