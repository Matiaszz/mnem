#include <iostream>
#include <cstdint>
#include <new>
#include <cstring>
#include "SlabAllocator.h"
using namespace std;

struct CacheEntry
{
    uint32_t keyHash;
    uint32_t ttl;
    char payload[56]; // Total: 64 bytes
};

int main()
{
    SlabAllocator allocator(sizeof(CacheEntry), 1000);

    cout << "Blocos livres iniciais: " << allocator.getFreeBlocks() << "\n";

    // Aloca dois blocos
    void *mem1 = allocator.allocate();
    void *mem2 = allocator.allocate();

    cout << "Blocos em uso: " << allocator.getAllocatedBlocks() << "\n";

    CacheEntry *entry1 = new (mem1) CacheEntry{0xDEADBEEF, 3600, "user:session:9812"};
    cout << "Entrada gravada: " << entry1->payload << "\n";
    cout << "Blocos usados depois da alocação: " << allocator.getFreeBlocks() << "\n";

    entry1->~CacheEntry();
    allocator.deallocate(mem1);
    allocator.deallocate(mem2);

    cout << "Blocos livres ao final: " << allocator.getFreeBlocks() << "\n";
    return 0;
}