#include "headers/ft/WriteOperation.h"
#include "headers/ft/FlatTable.h"
#include "headers/AOF.h"
#include <cstring>
#include <type_traits>
#include <string>

using namespace std;

// Construtor: vincula a operação à instância da FlatTable e inicializa o AOF
WriteOperation::WriteOperation(FlatTable &tbl, const string &aofPath)
    : table(tbl),
      aof(aofPath)
{
}

string WriteOperation::formatTokenForAof(string_view token)
{
    if (token.empty())
    {
        return "\"\"";
    }

    bool needsQuotes = false;
    for (char c : token)
    {
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '"' || c == '\'')
        {
            needsQuotes = true;
            break;
        }
    }

    if (!needsQuotes)
    {
        return string(token);
    }

    string escaped = "\"";
    escaped.reserve(token.size() + 4);
    for (char c : token)
    {
        if (c == '"' || c == '\\')
        {
            escaped += '\\';
        }
        escaped += c;
    }
    escaped += '"';
    return escaped;
}

string WriteOperation::formatValueForAof(const Value &val)
{
    string result;
    visit([&](const auto &v)
          {
        using T = decay_t<decltype(v)>;
        if constexpr (is_same_v<T, int64_t>)
        {
            result = to_string(v);
        }
        else if constexpr (is_same_v<T, double>)
        {
            string s = to_string(v);
            if (s.find('.') != string::npos)
            {
                while (s.size() > 1 && s.back() == '0' && *(s.rbegin() + 1) != '.')
                {
                    s.pop_back();
                }
            }
            result = s;
        }
        else if constexpr (is_same_v<T, string_view>)
        {
            result = formatTokenForAof(v);
        }
        else if constexpr (is_same_v<T, Binary>)
        {
            string_view binView(reinterpret_cast<const char *>(v.data()), v.size());
            result = formatTokenForAof(binView);
        }
        else if constexpr (is_same_v<T, Json>)
        {
            result = formatTokenForAof(v.data);
        } }, val);
    return result;
}

// Implementação interna de SET sem disparar escrita duplicada no AOF
bool WriteOperation::setInternal(string_view key, const Value &val, uint64_t expireAtMs)
{
    ValueType type = ValueType::STRING;
    string_view valBytes;

    visit([&](const auto &v)
          {
        using T = decay_t<decltype(v)>;
        if constexpr (is_same_v<T, int64_t>)
        {
            type = ValueType::INTEGER;
            valBytes = string_view(reinterpret_cast<const char *>(&v), sizeof(v));
        }
        else if constexpr (is_same_v<T, double>)
        {
            type = ValueType::FLOAT;
            valBytes = string_view(reinterpret_cast<const char *>(&v), sizeof(v));
        }
        else if constexpr (is_same_v<T, string_view>)
        {
            type = ValueType::STRING;
            valBytes = v;
        }
        else if constexpr (is_same_v<T, Binary>)
        {
            type = ValueType::BINARY;
            if (!v.empty())
            {
                valBytes = string_view(reinterpret_cast<const char *>(v.data()), v.size());
            }
        }
        else if constexpr (is_same_v<T, Json>)
        {
            type = ValueType::JSON;
            valBytes = v.data;
        } }, val);

    if (key.size() > UINT16_MAX || valBytes.size() > UINT16_MAX)
    {
        return false;
    }

    size_t totalBytes = sizeof(Item) + key.size() + valBytes.size();
    if (totalBytes > table.allocator.getBlockSize())
    {
        return false;
    }

    uint64_t h = FlatTable::hashKey(key);
    size_t idx = h & table.mask;
    size_t firstTombstone = table.capacity;

    for (size_t i = 0; i < table.capacity; ++i)
    {
        size_t probeIdx = (idx + i) & table.mask;
        FlatTable::Slot &slot = table.slots[probeIdx];

        if (slot.state == FlatTable::SlotState::OCCUPIED)
        {
            // Atualização de chave já existente
            if (slot.hash == h && slot.item->matches(key))
            {
                Item *newItem = table.writeItem(key, valBytes, type, expireAtMs);
                if (!newItem)
                    return false;

                table.allocator.deallocate(slot.item);
                slot.item = newItem;

                return true;
            }
        }
        else if (slot.state == FlatTable::SlotState::TOMBSTONE)
        {
            // Guarda o primeiro tombstone para reutilização caso a chave não exista
            if (firstTombstone == table.capacity)
            {
                firstTombstone = probeIdx;
            }
        }
        else
        { // SlotState::EMPTY
            // Chave não existe na tabela; insere no tombstone prévio ou no slot vazio atual
            size_t targetIdx = (firstTombstone != table.capacity) ? firstTombstone : probeIdx;
            Item *newItem = table.writeItem(key, valBytes, type, expireAtMs);
            if (!newItem)
                return false; // Slab esgotado

            table.slots[targetIdx] = {h, newItem, FlatTable::SlotState::OCCUPIED, {}};
            table.activeCount++;
            return true;
        }
    }

    // Tabela não possui slots vazios, mas encontrou um tombstone para reaproveitar
    if (firstTombstone != table.capacity)
    {
        Item *newItem = table.writeItem(key, valBytes, type, expireAtMs);
        if (!newItem)
            return false;

        table.slots[firstTombstone] = {h, newItem, FlatTable::SlotState::OCCUPIED, {}};
        ++table.activeCount;
        return true;
    }

    return false; // Tabela 100% cheia sem tombstones livres
}

// Insere ou atualiza uma chave na tabela (Comando SET) e registra no AOF
bool WriteOperation::set(string_view key, const Value &val, uint64_t expireAtMs)
{
    if (!setInternal(key, val, expireAtMs))
    {
        return false;
    }

    string op = "SET " + formatTokenForAof(key) + " " + formatValueForAof(val);
    if (expireAtMs > 0)
    {
        uint64_t now = FlatTable::getCurrentTimeMs();
        int64_t remainingMs = (expireAtMs > now) ? static_cast<int64_t>(expireAtMs - now) : 1;
        op += " PX " + to_string(remainingMs);
    }
    aof.append(op);
    return true;
}

// Atalho atômico SETEX: calcula o timestamp absoluto a partir de 'seconds', grava e registra no AOF
bool WriteOperation::setEx(string_view key, const Value &val, int64_t seconds)
{
    if (seconds <= 0)
        return false;

    uint64_t now = FlatTable::getCurrentTimeMs();
    if (!setInternal(key, val, now + static_cast<uint64_t>(seconds) * 1000))
    {
        return false;
    }

    aof.append("SET " + formatTokenForAof(key) + " " + formatValueForAof(val) + " EX " + to_string(seconds));
    return true;
}

// Atalho atômico PSETEX: calcula o timestamp absoluto a partir de 'milliseconds', grava e registra no AOF
bool WriteOperation::setPx(string_view key, const Value &val, int64_t milliseconds)
{
    if (milliseconds <= 0)
        return false;

    uint64_t now = FlatTable::getCurrentTimeMs();
    if (!setInternal(key, val, now + static_cast<uint64_t>(milliseconds)))
    {
        return false;
    }

    aof.append("SET " + formatTokenForAof(key) + " " + formatValueForAof(val) + " PX " + to_string(milliseconds));
    return true;
}

// Implementação interna de DEL sem disparar escrita no AOF
bool WriteOperation::delInternal(string_view key)
{
    size_t idx = table.findSlotIndex(key);
    if (idx == table.capacity)
    {
        return false;
    }

    FlatTable::Slot &slot = table.slots[idx];
    uint64_t now = FlatTable::getCurrentTimeMs();
    if (slot.item->isExpired(now))
    {
        table.deleteSlot(idx);
        return false;
    }

    table.deleteSlot(idx);
    return true;
}

// Exclui uma chave explicitamente (Comando DEL) e registra no AOF
bool WriteOperation::del(string_view key)
{
    if (!delInternal(key))
    {
        return false;
    }

    aof.append("DEL " + formatTokenForAof(key));
    return true;
}

// Implementação interna de substituição atômica de chave
bool WriteOperation::replaceKeyAtomicallyInternal(
    string_view key,
    string_view newKey,
    const Value &value,
    bool onlyIfDestinationMissing,
    uint64_t expireAtMs)
{
    if (key == newKey)
        return table.exists(key);

    if (onlyIfDestinationMissing && table.exists(newKey))
        return false;

    if (!setInternal(newKey, value, expireAtMs))
        return false;

    if (!delInternal(key))
    {
        return false;
    }

    return true;
}

// Renomeia 'key' para 'newKey' (Comando RENAME / RENAMENX) e registra no AOF
bool WriteOperation::rename(
    string_view key,
    string_view newKey,
    bool renameIfNotExist)
{
    if (key == newKey)
    {
        if (!table.exists(key))
            return false;
        aof.append(string(renameIfNotExist ? "RENAMENX " : "RENAME ") + formatTokenForAof(key) + " " + formatTokenForAof(newKey));
        return true;
    }

    size_t idx = table.findSlotIndex(key);
    if (idx == table.capacity)
        return false;

    FlatTable::Slot &slot = table.slots[idx];
    uint64_t now = FlatTable::getCurrentTimeMs();
    if (slot.item->isExpired(now))
    {
        table.deleteSlot(idx);
        return false;
    }

    uint64_t keyExpireAtMs = slot.item->expireAtMs;

    auto value = table.getValueCopy(key);
    if (!value)
        return false;

    if (!replaceKeyAtomicallyInternal(
            key,
            newKey,
            *value,
            renameIfNotExist,
            keyExpireAtMs))
    {
        return false;
    }

    aof.append(string(renameIfNotExist ? "RENAMENX " : "RENAME ") + formatTokenForAof(key) + " " + formatTokenForAof(newKey));
    return true;
}

// Substituição atômica de chave pública com registro no AOF
bool WriteOperation::replaceKeyAtomically(
    string_view key,
    string_view newKey,
    const Value &value,
    bool onlyIfDestinationMissing,
    uint64_t expireAtMs)
{
    if (!replaceKeyAtomicallyInternal(key, newKey, value, onlyIfDestinationMissing, expireAtMs))
    {
        return false;
    }

    aof.append(string(onlyIfDestinationMissing ? "RENAMENX " : "RENAME ") + formatTokenForAof(key) + " " + formatTokenForAof(newKey));
    return true;
}

bool WriteOperation::increment(string_view key, uint64_t quantity)
{
    if (!table.exists(key))
        return false;

    auto view = table.get(key);
    if (!view)
        return false;

    ValueType type = view->type;
    if (type != ValueType::INTEGER && type != ValueType::FLOAT)
        return false;

    auto storedValue = table.getValueCopy(key);
    if (!storedValue)
        return false;

    // Preserva o TTL existente da chave
    size_t idx = table.findSlotIndex(key);
    uint64_t keyExpireAtMs = (idx != table.capacity) ? table.slots[idx].item->expireAtMs : 0;

    Value value;
    if (type == ValueType::INTEGER)
    {
        int64_t currentValue = std::get<int64_t>(*storedValue);
        value = currentValue + static_cast<int64_t>(quantity);
    }
    else
    {
        double currentValue = std::get<double>(*storedValue);
        value = currentValue + static_cast<double>(quantity);
    }

    if (!setInternal(key, value, keyExpireAtMs))
    {
        return false;
    }

    if (quantity == 1)
    {
        aof.append("INCR " + formatTokenForAof(key));
    }
    else
    {
        aof.append("INCRBY " + formatTokenForAof(key) + " " + to_string(quantity));
    }
    return true;
}

bool WriteOperation::decrement(string_view key, uint64_t quantity)
{
    if (!table.exists(key))
        return false;

    auto view = table.get(key);
    if (!view)
        return false;

    ValueType type = view->type;
    if (type != ValueType::INTEGER && type != ValueType::FLOAT)
        return false;

    auto storedValue = table.getValueCopy(key);
    if (!storedValue)
        return false;

    // Preserva o TTL existente da chave
    size_t idx = table.findSlotIndex(key);
    uint64_t keyExpireAtMs = (idx != table.capacity) ? table.slots[idx].item->expireAtMs : 0;

    Value value;
    if (type == ValueType::INTEGER)
    {
        int64_t currentValue = std::get<int64_t>(*storedValue);
        value = currentValue - static_cast<int64_t>(quantity);
    }
    else
    {
        double currentValue = std::get<double>(*storedValue);
        value = currentValue - static_cast<double>(quantity);
    }

    if (!setInternal(key, value, keyExpireAtMs))
    {
        return false;
    }

    if (quantity == 1)
    {
        aof.append("DECR " + formatTokenForAof(key));
    }
    else
    {
        aof.append("DECRBY " + formatTokenForAof(key) + " " + to_string(quantity));
    }
    return true;
}
