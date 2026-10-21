#include "headers/SlabAllocator.h"
#include "platform/mem_mapping.h"
#include <cassert>
#include <string_view>
#include <new> // throw
#include <vector>

using namespace mnem::platform;

size_t SlabAllocator::alignUp(size_t size, size_t alignment)
{
    return (size + alignment - 1) & ~(alignment - 1);
}

SlabAllocator::SlabAllocator(size_t requestedBlockSize, size_t capacityInBlocks)
    : freeListHead(nullptr), allocatedBlocks(0), allocationMap(capacityInBlocks, 0)
{
    if (capacityInBlocks == 0)
        throw std::bad_alloc();

    size_t minSize = sizeof(FreeNode);
    size_t actualSize = requestedBlockSize < minSize ? minSize : requestedBlockSize;
    blockSize = alignUp(actualSize, 64);

    totalBlocks = capacityInBlocks;
    slabSize = blockSize * totalBlocks;

    // Aloca memória pelo mem mapping da plataforma
    memoryPool = allocate_anonymous(slabSize);

    if (!memoryPool)
    {
        throw std::bad_alloc();
    }

    // Constrói a lista intrusiva de blocos livres (Free-List)
    uint8_t *currentByte = static_cast<uint8_t *>(memoryPool);
    for (size_t i = 0; i < totalBlocks; ++i)
    {
        FreeNode *node = reinterpret_cast<FreeNode *>(currentByte);
        node->next = freeListHead;
        freeListHead = node;
        currentByte += blockSize;
    }
}

SlabAllocator::~SlabAllocator()
{
    if (memoryPool)
    {
        free_anonymous(memoryPool, slabSize);
    }
}

void *SlabAllocator::allocate()
{
    if (!freeListHead)
    {
        return nullptr;
    }

    FreeNode *node = freeListHead;
    freeListHead = freeListHead->next;

    size_t index = getPointerIndex(node);
    assert(allocationMap[index] == 0 && "Pointer already allocated");

    allocationMap[index] = 1;

    ++allocatedBlocks;

    return node;
}

size_t SlabAllocator::getPointerIndex(void *ptr)
{
    uintptr_t addr = reinterpret_cast<uintptr_t>(ptr);
    uintptr_t poolStart = reinterpret_cast<uintptr_t>(memoryPool);

    if (addr < poolStart || addr >= poolStart + slabSize)
        return totalBlocks;

    uintptr_t offset = addr - poolStart;

    if (offset % blockSize != 0)
        return totalBlocks;

    return offset / blockSize;
}

bool SlabAllocator::deallocate(void *ptr)
{
    if (!ptr)
        return false;

    uintptr_t addr = reinterpret_cast<uintptr_t>(ptr);
    uintptr_t poolStart = reinterpret_cast<uintptr_t>(memoryPool);

    uintptr_t poolEnd = poolStart + slabSize;

    if (addr < poolStart || addr >= poolStart + slabSize)
        return false;

    uintptr_t offset = addr - poolStart;
    if (offset % blockSize != 0)
        return false;

    size_t index = offset / blockSize;

    // Detecta double-free.
    if (allocationMap[index] == 0)
        return false;

    allocationMap[index] = 0;

    FreeNode *node = static_cast<FreeNode *>(ptr);

    node->next = freeListHead;
    freeListHead = node;
    --allocatedBlocks;

    return true;
}