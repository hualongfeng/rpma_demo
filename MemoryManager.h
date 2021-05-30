//
// Created by fenghl on 2021/5/29.
//

#ifndef RPMA_DEMO_MEMORYMANAGER_H
#define RPMA_DEMO_MEMORYMANAGER_H

#include <string>
#include <inttypes.h>

class MemoryManager {
public:
    MemoryManager(uint64_t size, string &path);
    MemoryManager() {}
    ~MemoryManager();
    void init(uint64_t size, string &path);
    void *get_pointer();
    uint64_t size() {return size;}
    bool is_pmem() { return is_pmem;}
private:
    void *get_memory_from_pmem(string &path);
    void *get_memory_from_dram();

    void *data{nullptr};
    uint64_t size;
    bool is_pmem;
};


#endif //RPMA_DEMO_MEMORYMANAGER_H
