//
// Created by fenghl on 2021/5/29.
//

#include "MemoryManager.h"
#include "common-conn.h"
#include <unistd.h>
#include <iostream>

MemoryManager::MemoryManager(uint64_t size, string &path) : size(size) {
    std::cout << "I'm in MemoryManager::MemoryManager()" << std::endl;
    data = get_memory_from_pmem(path);
    if (data != nullptr) {
        is_pmem = true;
        return;
    }

    data = get_memory_from_dram();
    if (data == nullptr) {
        throw std::runtime_error("memory malloc failed");
    }
}

void MemoryManager::init(uint64_t size, string &path) {
    std::cout << "I'm in MemoryManager::init()" << std::endl;
    this->size = size;
    data = get_memory_from_pmem(path);
    if (data != nullptr) {
        is_pmem = true;
        return;
    }

    data = get_memory_from_dram();
    if (data == nullptr) {
        throw std::runtime_error("memory malloc failed");
    }
}


MemoryManager::~MemoryManager() {
    std::cout << "I'm in MemoryManager::~MemoryManager()" << std::endl;
    if (data == nullptr) {
        return;
    }
    if (is_pmem) {
        pmem_unmap(data, size);
    } else {
        free(data);
    }
    data = nullptr;
}

void* MemoryManager::get_pointer() {
    return data;
}

void* MemoryManager::get_memory_from_pmem(string &path) {
    int pmem_memory;
    int len;
    if (access(path, F_OK) == 0) {
        data = pmem_map_file(path.c_str(), 0, 0, 0600, &len, &pmem_memory);
    }
    else {
        data = pmem_map_file(path.c_str(), size, PMEM_FILE_CREATE, 0600, &len, &(clnt->is_pmem));
    }
    if (!pmem_memory || len != size || data == NULL) {
        if (data) {
            pmem_unmap(data, size);
            data = nullptr;
        }
    }
    return data;
}

void* MemoryManager::get_memory_from_dram() {
    data = malloc_aligned(length);
    return data;
}
