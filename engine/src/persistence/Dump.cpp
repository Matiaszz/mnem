#include "headers/Dump.h"
#include "headers/ft/FlatTable.h"
#include <fstream>
#include <vector>
#include <cstring>

using namespace std;

namespace
{
    constexpr char DUMP_MAGIC[4] = {'M', 'N', 'E', 'M'};
    constexpr uint32_t DUMP_VERSION = 1;
}

Dump::Dump(const string &path)
    : filePath(path)
{
}

bool Dump::save(const FlatTable &table) const
{
    ofstream out(filePath, ios::out | ios::trunc | ios::binary);
    if (!out.is_open())
    {
        return false;
    }

    uint64_t now = FlatTable::getCurrentTimeMs();
    uint64_t validCount = 0;
    size_t cap = table.getCapacity();

    for (size_t i = 0; i < cap; ++i)
    {
        const FlatTable::Slot &slot = table.getSlot(i);
        if (slot.state == FlatTable::SlotState::OCCUPIED && slot.item != nullptr && !slot.item->isExpired(now))
        {
            ++validCount;
        }
    }

    // Cabeçalho: Magic (4B) + Versão (4B) + Quantidade de itens (8B)
    out.write(DUMP_MAGIC, sizeof(DUMP_MAGIC));
    out.write(reinterpret_cast<const char *>(&DUMP_VERSION), sizeof(DUMP_VERSION));
    out.write(reinterpret_cast<const char *>(&validCount), sizeof(validCount));

    for (size_t i = 0; i < cap; ++i)
    {
        const FlatTable::Slot &slot = table.getSlot(i);
        if (slot.state != FlatTable::SlotState::OCCUPIED || slot.item == nullptr)
        {
            continue;
        }

        const Item *it = slot.item;
        if (it->isExpired(now))
        {
            continue;
        }

        uint16_t keyLen = it->keyLen;
        uint16_t valLen = it->valLen;
        uint8_t type = static_cast<uint8_t>(it->type);
        uint64_t expireAtMs = it->expireAtMs;

        out.write(reinterpret_cast<const char *>(&keyLen), sizeof(keyLen));
        out.write(reinterpret_cast<const char *>(&valLen), sizeof(valLen));
        out.write(reinterpret_cast<const char *>(&type), sizeof(type));
        out.write(reinterpret_cast<const char *>(&expireAtMs), sizeof(expireAtMs));
        out.write(it->key(), keyLen);
        out.write(it->val(), valLen);
    }

    out.flush();
    return out.good();
}

bool Dump::load(FlatTable &table) const
{
    ifstream in(filePath, ios::in | ios::binary);
    if (!in.is_open())
    {
        return false;
    }

    char magic[4]{};
    uint32_t version = 0;
    uint64_t count = 0;

    if (!in.read(magic, sizeof(magic)) || memcmp(magic, DUMP_MAGIC, sizeof(DUMP_MAGIC)) != 0)
    {
        return false;
    }

    if (!in.read(reinterpret_cast<char *>(&version), sizeof(version)) || version != DUMP_VERSION)
    {
        return false;
    }

    if (!in.read(reinterpret_cast<char *>(&count), sizeof(count)))
    {
        return false;
    }

    uint64_t now = FlatTable::getCurrentTimeMs();

    for (uint64_t i = 0; i < count; ++i)
    {
        uint16_t keyLen = 0;
        uint16_t valLen = 0;
        uint8_t typeRaw = 0;
        uint64_t expireAtMs = 0;

        if (!in.read(reinterpret_cast<char *>(&keyLen), sizeof(keyLen)) ||
            !in.read(reinterpret_cast<char *>(&valLen), sizeof(valLen)) ||
            !in.read(reinterpret_cast<char *>(&typeRaw), sizeof(typeRaw)) ||
            !in.read(reinterpret_cast<char *>(&expireAtMs), sizeof(expireAtMs)))
        {
            return false;
        }

        string key(keyLen, '\0');
        string valBytes(valLen, '\0');

        if (keyLen > 0 && !in.read(key.data(), keyLen))
        {
            return false;
        }
        if (valLen > 0 && !in.read(valBytes.data(), valLen))
        {
            return false;
        }

        // Ignora chaves que expiraram enquanto o servidor estava desligado
        if (expireAtMs != 0 && now >= expireAtMs)
        {
            continue;
        }

        ValueView vv{static_cast<ValueType>(typeRaw), valBytes};
        auto optVal = vv.toValue();
        if (optVal.has_value())
        {
            table.write().restore(key, *optVal, expireAtMs);
        }
    }

    return true;
}
