#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#ifndef MNEM_SLABALLOCATOR_H
#define MNEM_SLABALLOCATOR_H

class SlabAllocator
{
public:
    struct SlabChunk
    {
        void *memoryPool{nullptr};
        size_t capacityInBlocks{0};
        size_t slabSize{0};
        size_t baseIndex{0};
        std::vector<uint8_t> allocationMap;
    };

private:
    struct FreeNode
    {
        FreeNode *next;
    };

    size_t blockSize;
    size_t initialCapacity;
    size_t growthFactor;

    std::vector<SlabChunk> chunks;
    FreeNode *freeListHead;

    size_t totalBlocks;
    size_t allocatedBlocks;

    static size_t alignUp(size_t size, size_t alignment);

    SlabChunk *findChunkForPointer(void *ptr);
    const SlabChunk *findChunkForPointer(const void *ptr) const;

public:
    // Construtor: define tamanho de bloco e capacidade inicial com estratégia de redimensionamento
    explicit SlabAllocator(size_t requestedBlockSize = 256, size_t capacityInBlocks = 1024, size_t growthFactor = 2);
    ~SlabAllocator();

    // Impede cópia (gerenciamento direto de mapeamento de memória da plataforma)
    SlabAllocator(const SlabAllocator &) = delete;
    SlabAllocator &operator=(const SlabAllocator &) = delete;

    // Movimentação permitida
    SlabAllocator(SlabAllocator &&other) noexcept;
    SlabAllocator &operator=(SlabAllocator &&other) noexcept;

    // Aloca um bloco contíguo de tamanho blockSize. Se a capacidade esgotar, aciona redimensionamento automático.
    void *allocate();

    // Libera um bloco previamente alocado de volta para a Free-List
    bool deallocate(void *ptr);

    // Retorna o índice global do bloco apontado por ptr ou totalBlocks se inválido
    size_t getPointerIndex(void *ptr);

    // Estratégia de redimensionamento: aloca um novo slab de memória e adiciona os blocos à free-list.
    // Se blocksCount for 0, usa a capacidade do último chunk multiplicado por growthFactor.
    bool grow(size_t blocksCount = 0);

    size_t getBlockSize() const { return blockSize; }
    size_t getTotalBlocks() const { return totalBlocks; }
    size_t getAllocatedBlocks() const { return allocatedBlocks; }
    size_t getFreeBlocks() const { return totalBlocks - allocatedBlocks; }
    size_t getChunkCount() const { return chunks.size(); }
    size_t getGrowthFactor() const { return growthFactor; }
    void setGrowthFactor(size_t factor) { growthFactor = factor > 0 ? factor : 1; }
};

#endif // MNEM_SLABALLOCATOR_H
