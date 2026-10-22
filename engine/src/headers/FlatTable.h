#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>
#include "headers/SlabAllocator.h"
#include <vector>
#include <optional>
#include <variant>
#include <ostream>
#include <cstring>

#ifndef MNEM_FlatTable_H
#define MNEM_FlatTable_H

enum class ValueType : std::uint8_t
{
    STRING,
    INTEGER,
    FLOAT,
    JSON,
    BINARY
};

struct alignas(8) Item
{
    uint16_t keyLen;
    uint16_t valLen;
    ValueType type;

    const char *key() const;
    const char *val() const;
    bool matches(std::string_view k) const;
};

using Binary = std::vector<std::uint8_t>;

struct Json
{
    std::string_view data;
};

using Value = std::variant<
    std::int64_t,
    double,
    std::string_view,
    Binary,
    Json>;

struct ValueView
{
    ValueType type;
    std::string_view bytes;

    std::optional<std::int64_t> asInt() const
    {
        if (type == ValueType::INTEGER && bytes.size() == sizeof(std::int64_t))
        {
            std::int64_t v;
            std::memcpy(&v, bytes.data(), sizeof(v));
            return v;
        }
        return std::nullopt;
    }

    std::optional<double> asFloat() const
    {
        if (type == ValueType::FLOAT && bytes.size() == sizeof(double))
        {
            double v;
            std::memcpy(&v, bytes.data(), sizeof(v));
            return v;
        }
        return std::nullopt;
    }

    std::string_view asString() const
    {
        return bytes;
    }

    std::optional<Value> toValue() const
    {
        switch (type)
        {
        case ValueType::INTEGER:
            if (auto v = asInt())
                return Value{*v};
            break;

        case ValueType::FLOAT:
            if (auto v = asFloat())
                return Value{*v};
            break;

        case ValueType::STRING:
            return Value{bytes};

        case ValueType::JSON:
            return Value{Json{bytes}};

        case ValueType::BINARY:
            return Value{Binary(bytes.begin(), bytes.end())};
        }

        return std::nullopt;
    }
};

inline std::ostream &operator<<(std::ostream &os, const ValueView &vv)
{
    switch (vv.type)
    {
    case ValueType::INTEGER:
        if (auto v = vv.asInt())
            return os << *v;
        break;
    case ValueType::FLOAT:
        if (auto v = vv.asFloat())
            return os << *v;
        break;
    default:
        break;
    }
    return os << vv.bytes;
}

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

public:
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

    FlatTable(size_t pow2Capacity, SlabAllocator &alloc);

    bool set(std::string_view key, const Value &val);

    std::optional<ValueView> get(std::string_view key);

    bool rename(std::string_view key, std::string_view newKey, bool renameIfNotExist);

    bool del(std::string_view key);

    bool exists(std::string_view key);

    std::optional<Value> getValueCopy(std::string_view key);

    bool replaceKeyAtomically(
        std::string_view key,
        std::string_view newKey,
        const Value &value,
        bool onlyIfDestinationMissing);

    size_t getActiveCount() const { return activeCount; }
    size_t getCapacity() const { return capacity; }
    SlotState getSlotState(size_t idx) const { return slots[idx].state; }
    const Slot &getSlot(size_t idx) const { return slots[idx]; }

private:
    Item *writeItem(std::string_view key, std::string_view val, ValueType type = ValueType::STRING);
};

#endif // MNEM_FlatTable_H
