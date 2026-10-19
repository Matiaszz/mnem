#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>
#include "headers/SlabAllocator.h"
#include <vector>
#include <optional>

#ifndef MNEM_FlatTable_H
#define MNEM_FlatTable_H

struct alignas(8) Item
{
    uint16_t keyLen;
    uint16_t valLen;

    const char *key() const;
    const char *val() const;
    const bool matches(std::string_view k) const;
};

class FlatTable
{
public:
    enum class SlotState : uint8_t
    {
        EMPTY = 0,
        OCCUPIED = 1,
        TOMBSTONE = 2
    };

    struct Slot
    {
        uint64_t hash;
        Item *item;
        SlotState state;
        uint8_t padding[7];
    };

private:
    std::vector<Slot> slots;
    size_t capacity;
    size_t mask;
    size_t activeCount;
    SlabAllocator &allocator;

    // FNV-1a Hash de 64 bits
    static uint64_t hashKey(std::string_view key)
    {
        uint64_t hash = 14695981039346656037ULL;
        for (char c : key)
        {
            hash ^= static_cast<uint8_t>(c);
            hash *= 1099511628211ULL;
        }
        return hash;
    }

public:
    FlatTable(size_t pow2Capacity, SlabAllocator &alloc);

    bool set(std::string_view key, std::string_view val);

    std::optional<std::string_view> get(std::string_view key);

    bool del(std::string_view key);

private:
    Item *writeItem(std::string_view key, std::string_view val);
};

#endif // MNEM_FlatTable_H
