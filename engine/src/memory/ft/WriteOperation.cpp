#include "headers/ft/WriteOperation.h"
#include "headers/ft/FlatTable.h"
#include <cstring>
#include <type_traits>

using namespace std;

// Construtor: vincula a operação à instância da FlatTable
WriteOperation::WriteOperation(FlatTable &tbl)
    : table(tbl)
{
}

// Insere ou atualiza uma chave na tabela (Comando SET do Redis):
// 1. Extrai a representação em bytes do tipo Value (variant).
// 2. Se a chave já existir (slot OCCUPIED com mesma chave), substitui o item na mesma posição.
// 3. Se for uma nova inserção, reutiliza o primeiro TOMBSTONE encontrado durante a sondagem
//    ou insere no primeiro slot EMPTY.
// 4. Atribui expireAtMs ao novo item (se 0, limpa qualquer TTL anterior).
bool WriteOperation::set(string_view key, const Value &val, uint64_t expireAtMs)
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

// Atalho atômico SETEX: calcula o timestamp absoluto a partir de 'seconds' e invoca set()
bool WriteOperation::setEx(string_view key, const Value &val, int64_t seconds)
{
    if (seconds <= 0)
        return false;

    uint64_t now = FlatTable::getCurrentTimeMs();
    return set(key, val, now + static_cast<uint64_t>(seconds) * 1000);
}

// Atalho atômico PSETEX: calcula o timestamp absoluto a partir de 'milliseconds' e invoca set()
bool WriteOperation::setPx(string_view key, const Value &val, int64_t milliseconds)
{
    if (milliseconds <= 0)
        return false;

    uint64_t now = FlatTable::getCurrentTimeMs();
    return set(key, val, now + static_cast<uint64_t>(milliseconds));
}

// Exclui uma chave explicitamente (Comando DEL do Redis):
// Se a chave já estiver expirada, limpa o slot e retorna false (no Redis, DEL de chave expirada retorna 0).
// Se estiver ativa, remove o slot e retorna true.
bool WriteOperation::del(string_view key)
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

// Renomeia 'key' para 'newKey' (Comando RENAME / RENAMENX do Redis):
// Preserva tanto o valor quanto o TTL restante da chave original no novo nome.
bool WriteOperation::rename(
    string_view key,
    string_view newKey,
    bool renameIfNotExist)
{
    if (key == newKey)
        return table.exists(key);

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

    return replaceKeyAtomically(
        key,
        newKey,
        *value,
        renameIfNotExist,
        keyExpireAtMs);
}

// Substituição atômica de chave:
// Insere a nova chave com o valor e o timestamp de expiração fornecido;
// se obtiver sucesso, remove a chave antiga.
bool WriteOperation::replaceKeyAtomically(
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

    if (!set(newKey, value, expireAtMs))
        return false;

    if (!del(key))
    {
        return false;
    }

    return true;
}
