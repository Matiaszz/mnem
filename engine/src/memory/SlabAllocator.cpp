#include "headers/SlabAllocator.h"
#include "platform/mem_mapping.h"
#include <cassert>
#include <string_view>
#include <new> // bad_alloc
#include <vector>
#include <utility>

using namespace mnem::platform;

size_t SlabAllocator::alignUp(size_t size, size_t alignment)
{
    return (size + alignment - 1) & ~(alignment - 1);
}

SlabAllocator::SlabAllocator(size_t requestedBlockSize, size_t capacityInBlocks, size_t growthFactor)
    : freeListHead(nullptr),
      totalBlocks(0),
      allocatedBlocks(0),
      initialCapacity(capacityInBlocks > 0 ? capacityInBlocks : 1),
      growthFactor(growthFactor > 0 ? growthFactor : 2)
{
    if (capacityInBlocks == 0)
        throw std::bad_alloc();

    size_t minSize = sizeof(FreeNode);
    size_t actualSize = requestedBlockSize < minSize ? minSize : requestedBlockSize;
    blockSize = alignUp(actualSize, 64);

    if (!grow(capacityInBlocks))
    {
        throw std::bad_alloc();
    }
}

SlabAllocator::~SlabAllocator()
{
    for (auto &chunk : chunks)
    {
        if (chunk.memoryPool)
        {
            free_anonymous(chunk.memoryPool, chunk.slabSize);
            chunk.memoryPool = nullptr;
        }
    }
    chunks.clear();
    freeListHead = nullptr;
}

SlabAllocator::SlabAllocator(SlabAllocator &&other) noexcept
    : blockSize(other.blockSize),
      initialCapacity(other.initialCapacity),
      growthFactor(other.growthFactor),
      chunks(std::move(other.chunks)),
      freeListHead(other.freeListHead),
      totalBlocks(other.totalBlocks),
      allocatedBlocks(other.allocatedBlocks)
{
    other.freeListHead = nullptr;
    other.totalBlocks = 0;
    other.allocatedBlocks = 0;
}

SlabAllocator &SlabAllocator::operator=(SlabAllocator &&other) noexcept
{
    if (this != &other)
    {
        for (auto &chunk : chunks)
        {
            if (chunk.memoryPool)
            {
                free_anonymous(chunk.memoryPool, chunk.slabSize);
                chunk.memoryPool = nullptr;
            }
        }
        chunks.clear();

        blockSize = other.blockSize;
        initialCapacity = other.initialCapacity;
        growthFactor = other.growthFactor;
        chunks = std::move(other.chunks);
        freeListHead = other.freeListHead;
        totalBlocks = other.totalBlocks;
        allocatedBlocks = other.allocatedBlocks;

        other.freeListHead = nullptr;
        other.totalBlocks = 0;
        other.allocatedBlocks = 0;
    }
    return *this;
}

bool SlabAllocator::grow(size_t blocksCount)
{
    if (blocksCount == 0)
    {
        if (chunks.empty())
        {
            blocksCount = initialCapacity;
        }
        else
        {
            blocksCount = chunks.back().capacityInBlocks * growthFactor;
        }
    }

    if (blocksCount == 0)
    {
        return false;
    }

    size_t newSlabSize = blockSize * blocksCount;
    void *newPool = allocate_anonymous(newSlabSize);
    if (!newPool)
    {
        return false;
    }

    SlabChunk chunk;
    chunk.memoryPool = newPool;
    chunk.capacityInBlocks = blocksCount;
    chunk.slabSize = newSlabSize;
    chunk.baseIndex = totalBlocks;
    chunk.allocationMap.assign(blocksCount, 0);

    // Constrói a lista intrusiva de blocos livres para o novo chunk
    uint8_t *currentByte = static_cast<uint8_t *>(newPool);
    for (size_t i = 0; i < blocksCount; ++i)
    {
        FreeNode *node = reinterpret_cast<FreeNode *>(currentByte);
        node->next = freeListHead;
        freeListHead = node;
        currentByte += blockSize;
    }

    totalBlocks += blocksCount;
    chunks.push_back(std::move(chunk));

    return true;
}

SlabAllocator::SlabChunk *SlabAllocator::findChunkForPointer(void *ptr)
{
    if (!ptr)
        return nullptr;

    uintptr_t addr = reinterpret_cast<uintptr_t>(ptr);
    for (auto &chunk : chunks)
    {
        uintptr_t start = reinterpret_cast<uintptr_t>(chunk.memoryPool);
        if (addr >= start && addr < start + chunk.slabSize)
        {
            return &chunk;
        }
    }
    return nullptr;
}

const SlabAllocator::SlabChunk *SlabAllocator::findChunkForPointer(const void *ptr) const
{
    if (!ptr)
        return nullptr;

    uintptr_t addr = reinterpret_cast<uintptr_t>(ptr);
    for (const auto &chunk : chunks)
    {
        uintptr_t start = reinterpret_cast<uintptr_t>(chunk.memoryPool);
        if (addr >= start && addr < start + chunk.slabSize)
        {
            return &chunk;
        }
    }
    return nullptr;
}

void *SlabAllocator::allocate()
{
    // Se a free list estiver esgotada, executa a estratégia de redimensionamento
    if (!freeListHead)
    {
        if (!grow())
        {
            return nullptr;
        }
    }

    FreeNode *node = freeListHead;
    freeListHead = freeListHead->next;

    SlabChunk *chunk = findChunkForPointer(node);
    assert(chunk != nullptr);

    uintptr_t offset = reinterpret_cast<uintptr_t>(node) - reinterpret_cast<uintptr_t>(chunk->memoryPool);
    size_t index = offset / blockSize;
    assert(index < chunk->capacityInBlocks);
    assert(chunk->allocationMap[index] == 0 && "Pointer already allocated");

    chunk->allocationMap[index] = 1;
    ++allocatedBlocks;

    return node;
}

size_t SlabAllocator::getPointerIndex(void *ptr)
{
    if (!ptr)
        return totalBlocks;

    const SlabChunk *chunk = findChunkForPointer(ptr);
    if (!chunk)
        return totalBlocks;

    uintptr_t addr = reinterpret_cast<uintptr_t>(ptr);
    uintptr_t poolStart = reinterpret_cast<uintptr_t>(chunk->memoryPool);
    uintptr_t offset = addr - poolStart;

    if (offset % blockSize != 0)
        return totalBlocks;

    size_t index = offset / blockSize;
    if (index >= chunk->capacityInBlocks)
        return totalBlocks;

    return chunk->baseIndex + index;
}

bool SlabAllocator::deallocate(void *ptr)
{
    if (!ptr)
        return false;

    SlabChunk *chunk = findChunkForPointer(ptr);
    if (!chunk)
        return false;

    uintptr_t addr = reinterpret_cast<uintptr_t>(ptr);
    uintptr_t poolStart = reinterpret_cast<uintptr_t>(chunk->memoryPool);
    uintptr_t offset = addr - poolStart;

    if (offset % blockSize != 0)
        return false;

    size_t index = offset / blockSize;
    if (index >= chunk->capacityInBlocks)
        return false;

    // Detecta double-free
    if (chunk->allocationMap[index] == 0)
        return false;

    chunk->allocationMap[index] = 0;

    FreeNode *node = static_cast<FreeNode *>(ptr);
    node->next = freeListHead;
    freeListHead = node;
    --allocatedBlocks;

    return true;
}
