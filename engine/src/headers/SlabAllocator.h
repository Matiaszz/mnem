#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#ifndef MNEM_SLABALLOCATOR_H
#define MNEM_SLABALLOCATOR_H

class SlabAllocator
{
private:
    struct FreeNode
    {
        FreeNode *next;
    };

    size_t blockSize;
    size_t slabSize;
    void *memoryPool;
    FreeNode *freeListHead;

    size_t totalBlocks;
    size_t allocatedBlocks;

    static size_t alignUp(size_t size, size_t alignment);
    std::vector<uint8_t> allocationMap;

public:
    SlabAllocator(size_t requestedBlockSize, size_t capacityInBlocks);
    ~SlabAllocator();

    void *allocate();
    bool deallocate(void *ptr);
    size_t getPointerIndex(void *ptr);

    size_t getBlockSize() const { return blockSize; }
    size_t getTotalBlocks() const { return totalBlocks; }
    size_t getAllocatedBlocks() const { return allocatedBlocks; }
    size_t getFreeBlocks() const { return totalBlocks - allocatedBlocks; }
};

#endif // MNEM_SLABALLOCATOR_H
