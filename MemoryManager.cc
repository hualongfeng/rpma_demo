//
// Created by fenghl on 2021/5/29.
//

#include "MemoryManager.h"
#include "common-conn.h"
#include <unistd.h>
#include <iostream>
#include <libpmem.h>

MemoryManager::MemoryManager(uint64_t size, std::string &path) : _size(size) {
  std::cout << "I'm in MemoryManager::MemoryManager()" << std::endl;
  _data = get_memory_from_pmem(path);
  if (_data != nullptr) {
    _is_pmem = true;
    return;
  }

  _data = get_memory_from_dram();
  if (_data == nullptr) {
    throw std::runtime_error("memory malloc failed");
  }
}

void MemoryManager::init(uint64_t size, std::string &path) {
  std::cout << "I'm in MemoryManager::init()" << std::endl;
  path = "/mnt/pmem/" + path;
  std::cout << "path: " << path << std::endl;
  _size = size;
  _data = get_memory_from_pmem(path);
  if (_data != nullptr) {
    _is_pmem = true;
    return;
  }

  _data = get_memory_from_dram();
  if (_data == nullptr) {
    throw std::runtime_error("memory malloc failed");
  }
}


MemoryManager::~MemoryManager() {
  std::cout << "I'm in MemoryManager::~MemoryManager()" << std::endl;
  if (_data == nullptr) {
    return;
  }
  if (_is_pmem) {
    pmem_unmap(_data, _size);
  } else {
    free(_data);
  }
  _data = nullptr;
}

void* MemoryManager::get_pointer() {
  return _data;
}

void* MemoryManager::get_memory_from_pmem(std::string &path) {
  if (path.empty()) {
    return nullptr;
  }
  size_t len;
  int is_pmem;
  if (access(path.c_str(), F_OK) == 0) {
    _data = pmem_map_file(path.c_str(), 0, 0, 0600, &len, &is_pmem);
  }
  else {
    _data = pmem_map_file(path.c_str(), _size, PMEM_FILE_CREATE, 0600, &len, &is_pmem);
  }
  if (!is_pmem || len != _size || _data == NULL) {
    if (_data) {
      pmem_unmap(_data, _size);
      _data = nullptr;
    }
  }
  return _data;
}

void* MemoryManager::get_memory_from_dram() {
  _data = malloc_aligned(_size);
  return _data;
}
