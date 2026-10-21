#include "headers/FlatTable.h"
#include <string_view>
#include <cassert>
#include <cstring>
#include <cstdint>
#include <type_traits>
#include <variant>

using namespace std;

const char *Item::key() const
{
    return reinterpret_cast<const char *>(this + 1);
}

const char *Item::val() const
{
    return key() + keyLen;
}

bool Item::matches(string_view k) const
{
    return keyLen == k.size() && memcmp(key(), k.data(), k.size()) == 0;
}

FlatTable::FlatTable(size_t pow2Capacity, SlabAllocator &alloc)
    : capacity(pow2Capacity), mask(pow2Capacity - 1), activeCount(0), allocator(alloc)
{
    assert(((pow2Capacity & (pow2Capacity - 1)) == 0 && pow2Capacity > 0) && "Capacity must be a non-zero power of 2");
    slots.resize(capacity, {0, nullptr, SlotState::EMPTY, {}});
}

Item *FlatTable::writeItem(std::string_view key, std::string_view val, ValueType type)
{
    void *rawMem = allocator.allocate();
    if (!rawMem)
        return nullptr;

    Item *it = static_cast<Item *>(rawMem);
    it->keyLen = static_cast<uint16_t>(key.size());
    it->valLen = static_cast<uint16_t>(val.size());
    it->type = type;

    char *payload = reinterpret_cast<char *>(it + 1);
    std::memcpy(payload, key.data(), key.size());
    std::memcpy(payload + key.size(), val.data(), val.size());

    return it;
}

optional<ValueView> FlatTable::get(string_view key)
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
                return ValueView{
                    slot.item->type,
                    std::string_view(slot.item->val(), slot.item->valLen)
                };
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
        Slot &slot = slots[(idx + i) & mask];

        if (slot.state == SlotState::EMPTY)
        {
            return false; // Fim da cadeia de sondagem
        }

        if (slot.state == SlotState::OCCUPIED && slot.hash == h)
        {
            if (slot.item->matches(key))
            {
                allocator.deallocate(slot.item);

                slot.item = nullptr;
                slot.state = SlotState::TOMBSTONE;
                --activeCount;

                return true;
            }
        }
    }

    return false;
}

bool FlatTable::set(string_view key, const Value &val)
{
    ValueType type = ValueType::STRING;
    std::string_view valBytes;

    std::visit([&](const auto &v) {
        using T = std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<T, std::int64_t>)
        {
            type = ValueType::INTEGER;
            valBytes = std::string_view(reinterpret_cast<const char *>(&v), sizeof(v));
        }
        else if constexpr (std::is_same_v<T, double>)
        {
            type = ValueType::FLOAT;
            valBytes = std::string_view(reinterpret_cast<const char *>(&v), sizeof(v));
        }
        else if constexpr (std::is_same_v<T, std::string_view>)
        {
            type = ValueType::STRING;
            valBytes = v;
        }
        else if constexpr (std::is_same_v<T, Binary>)
        {
            type = ValueType::BINARY;
            if (!v.empty())
            {
                valBytes = std::string_view(reinterpret_cast<const char *>(v.data()), v.size());
            }
        }
        else if constexpr (std::is_same_v<T, Json>)
        {
            type = ValueType::JSON;
            valBytes = v.data;
        }
    }, val);

    if (key.size() > UINT16_MAX || valBytes.size() > UINT16_MAX)
    {
        return false;
    }

    size_t totalBytes = sizeof(Item) + key.size() + valBytes.size();
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
                Item *newItem = writeItem(key, valBytes, type);
                if (!newItem)
                    return false;

                allocator.deallocate(slot.item);
                slot.item = newItem;

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
            size_t targetIdx = (firstTombstone != capacity) ? firstTombstone : probeIdx;
            Item *newItem = writeItem(key, valBytes, type);
            if (!newItem)
                return false; // Slab esgotado

            slots[targetIdx] = {h, newItem, SlotState::OCCUPIED, {}};
            activeCount++;
            return true;
        }
    }

    if (firstTombstone != capacity)
    {
        Item *newItem = writeItem(key, valBytes, type);
        if (!newItem)
            return false;

        slots[firstTombstone] = {h, newItem, SlotState::OCCUPIED, {}};
        ++activeCount;
        return true;
    }

    return false;
}
