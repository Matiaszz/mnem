#include "headers/ft/FlatTable.h"
#include <string_view>
#include <cassert>
#include <cstring>
#include <cstdint>
#include <memory>

using namespace std;

// Retorna o ponteiro para os bytes da chave, armazenados logo após o cabeçalho Item
const char *Item::key() const
{
    return reinterpret_cast<const char *>(this + 1);
}

// Retorna o ponteiro para os bytes do valor, armazenados logo após os bytes da chave
const char *Item::val() const
{
    return key() + keyLen;
}

// Verifica se a chave armazenada é idêntica a string_view fornecida
bool Item::matches(string_view k) const
{
    return keyLen == k.size() && memcmp(key(), k.data(), k.size()) == 0;
}

// Construtor autônomo da tabela hash plana:
// Cria e gerencia internamente sua própria instância de SlabAllocator com suporte a redimensionamento.
FlatTable::FlatTable(size_t pow2Capacity, size_t slabBlockSize)
    : capacity(pow2Capacity),
      mask(pow2Capacity - 1),
      activeCount(0),
      ownedAllocator(make_unique<SlabAllocator>(slabBlockSize, pow2Capacity > 0 ? pow2Capacity : 1024)),
      allocator(*ownedAllocator),
      readOp(*this),
      writeOp(*this),
      ttlOp(*this)
{
    assert(((pow2Capacity & (pow2Capacity - 1)) == 0 && pow2Capacity > 0) && "Capacity must be a non-zero power of 2");
    slots.resize(capacity, {0, nullptr, SlotState::EMPTY, {}});
}

// Construtor alternativo: vincula a uma instância externa de SlabAllocator
FlatTable::FlatTable(size_t pow2Capacity, SlabAllocator &alloc)
    : capacity(pow2Capacity),
      mask(pow2Capacity - 1),
      activeCount(0),
      ownedAllocator(nullptr),
      allocator(alloc),
      readOp(*this),
      writeOp(*this),
      ttlOp(*this)
{
    assert(((pow2Capacity & (pow2Capacity - 1)) == 0 && pow2Capacity > 0) && "Capacity must be a non-zero power of 2");
    slots.resize(capacity, {0, nullptr, SlotState::EMPTY, {}});
}

FlatTable::~FlatTable()
{
    for (size_t i = 0; i < capacity; ++i)
    {
        if (slots[i].state == SlotState::OCCUPIED && slots[i].item != nullptr)
        {
            allocator.deallocate(slots[i].item);
            slots[i].item = nullptr;
        }
    }
}

// Escreve um item na memória alocada do Slab:
// Aloca um bloco contíguo de tamanho fixo, preenche os metadados do cabeçalho Item
// (incluindo o timestamp de expiração expireAtMs) e copia os bytes de key e val em sequência.
Item *FlatTable::writeItem(string_view key, string_view val, ValueType type, uint64_t expireAtMs)
{
    void *rawMem = allocator.allocate();
    if (!rawMem)
        return nullptr;

    Item *it = static_cast<Item *>(rawMem);
    it->keyLen = static_cast<uint16_t>(key.size());
    it->valLen = static_cast<uint16_t>(val.size());
    it->type = type;
    it->padding[0] = 0;
    it->padding[1] = 0;
    it->padding[2] = 0;
    it->expireAtMs = expireAtMs;

    char *payload = reinterpret_cast<char *>(it + 1);
    memcpy(payload, key.data(), key.size());
    memcpy(payload + key.size(), val.data(), val.size());

    return it;
}

// Localiza o índice do slot de uma chave usando sondagem linear (linear probing):
// - Se encontrar um slot EMPTY, a chave não existe e a busca encerra (retorna capacity).
// - Se encontrar um slot TOMBSTONE, continua sondando adiante.
// - Se encontrar um slot OCCUPIED com hash e chave correspondentes, retorna o índice.
size_t FlatTable::findSlotIndex(string_view key) const
{
    uint64_t h = hashKey(key);
    size_t idx = h & mask;

    for (size_t i = 0; i < capacity; ++i)
    {
        size_t probeIdx = (idx + i) & mask;
        const Slot &slot = slots[probeIdx];

        if (slot.state == SlotState::EMPTY)
        {
            return capacity; // Fim da cadeia de busca
        }

        if (slot.state == SlotState::OCCUPIED && slot.hash == h)
        {
            if (slot.item->matches(key))
            {
                return probeIdx; // Encontrado
            }
        }
    }

    return capacity;
}

// Remove o item no slot 'index', liberando o bloco no SlabAllocator,
// marcando o slot como TOMBSTONE (para não quebrar a cadeia de busca)
// e decrementando a contagem de chaves ativas (activeCount).
void FlatTable::deleteSlot(size_t index)
{
    if (index >= capacity)
        return;

    Slot &slot = slots[index];
    if (slot.state == SlotState::OCCUPIED && slot.item != nullptr)
    {
        allocator.deallocate(slot.item);
        slot.item = nullptr;
        slot.state = SlotState::TOMBSTONE;
        --activeCount;
    }
}
