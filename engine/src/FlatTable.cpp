#include "headers/FlatTable.h"
#include <string_view>
#include <cassert>
#include <cstring>

using namespace std;

const char *Item::key() const
{
    return reinterpret_cast<const char *>(this + 1);
}

const char *Item::val() const
{
    return key() + keyLen;
}

const bool Item::matches(string_view k) const
{
    return keyLen == k.size() && memcmp(key(), k.data(), k.size()) == 0;
}

FlatTable::FlatTable(size_t pow2Capacity, SlabAllocator &alloc)
    : capacity(pow2Capacity), mask(pow2Capacity - 1), activeCount(0), allocator(alloc)
{
    assert((pow2Capacity & (pow2Capacity - 1)) == 0 && "Capacity must be pow of 2");
    slots.resize(capacity, {0, nullptr, SlotState::EMPTY, {}});
}

Item *FlatTable::writeItem(std::string_view key, std::string_view val)
{
    void *rawMem = allocator.allocate();
    if (!rawMem)
        return nullptr;

    Item *it = static_cast<Item *>(rawMem);
    it->keyLen = static_cast<uint16_t>(key.size());
    it->valLen = static_cast<uint16_t>(val.size());

    char *payload = reinterpret_cast<char *>(it + 1);
    std::memcpy(payload, key.data(), key.size());
    std::memcpy(payload + key.size(), val.data(), val.size());

    return it;
}

optional<string_view> FlatTable::get(string_view key)
{

    uint64_t h = hashKey(key);
    size_t idx = h & mask;

    for (size_t i = 0; i < capacity; ++i)
    {
        const Slot &slot = slots[(idx + i) & mask];

        if (slot.state == SlotState::EMPTY)
        {
            return nullopt; // Fim da cadeia de sondagem
        }

        if (slot.state == SlotState::OCCUPIED && slot.hash == h)
        {
            if (slot.item->matches(key))
            {
                return string_view(slot.item->val(), slot.item->valLen);
            }
        }
    }

    return nullopt;
}

bool FlatTable::del(std::string_view key)
{
    uint64_t h = hashKey(key);
    size_t idx = h & mask;

    for (size_t i = 0; i < capacity; ++i)
    {
        const Slot &slot = slots[(idx + i) & mask];

        if (slot.state == SlotState::EMPTY)
        {
            return false; // Fim da cadeia de sondagem
        }

        if (slot.state == SlotState::OCCUPIED && slot.hash == h)
        {
            if (slot.item->matches(key))
            {
                allocator.deallocate(slot.item);
                return true;
            }
        }
    }

    return false;
}

bool FlatTable::set(string_view key, string_view val)
{
    size_t totalBytes = sizeof(Item) + key.size() + val.size();
    if (totalBytes > allocator.getBlockSize())
    {
        return false;
    }

    uint64_t h = hashKey(key);
    size_t idx = h & mask;
    size_t firstTombstone = capacity;

    for (size_t i = 0; i < capacity; ++i)
    {
        size_t probeIdx = (idx + i) & mask;
        Slot &slot = slots[probeIdx];

        if (slot.state == SlotState::OCCUPIED)
        {
            if (slot.hash == h && slot.item->matches(key))
            {
                allocator.deallocate(slot.item);
                slot.item = writeItem(key, val);
                return true;
            }
        }
        else if (slot.state == SlotState::TOMBSTONE)
        {
            if (firstTombstone == capacity)
            {
                firstTombstone = probeIdx;
            }
        }
        else
        { // SlotState::EMPTY
            size_t targetIdx = firstTombstone != capacity ? firstTombstone : probeIdx;
            Item *newItem = writeItem(key, val);
            if (!newItem)
                return false; // Slab esgotado

            slots[targetIdx] = {h, newItem, SlotState::OCCUPIED, {}};
            activeCount++;
            return true;
        }
    }

    return false;
}
