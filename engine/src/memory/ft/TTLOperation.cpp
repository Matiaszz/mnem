#include "headers/ft/TTLOperation.h"
#include "headers/ft/FlatTable.h"

using namespace std;

// Construtor: vincula a operação à instância da FlatTable
TTLOperation::TTLOperation(FlatTable &tbl)
    : table(tbl)
{
}

// Define timeout em segundos para uma chave (Comando EXPIRE):
// Converte os segundos em milissegundos e delega para pexpire.
bool TTLOperation::expire(string_view key, int64_t seconds)
{
    if (seconds <= 0)
    {
        return pexpire(key, 0);
    }
    if (seconds > static_cast<int64_t>(UINT64_MAX / 1000))
    {
        return pexpire(key, INT64_MAX);
    }
    return pexpire(key, seconds * 1000);
}

// Define timeout em milissegundos para uma chave (Comando PEXPIRE):
// - Se a chave não existir ou estiver expirada, retorna false.
// - Se milliseconds <= 0, remove a chave imediatamente e retorna true.
// - Caso contrário, armazena no Item o timestamp absoluto de expiração (now + milliseconds).
bool TTLOperation::pexpire(string_view key, int64_t milliseconds)
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

    if (milliseconds <= 0)
    {
        table.deleteSlot(idx);
        return true;
    }

    slot.item->expireAtMs = now + static_cast<uint64_t>(milliseconds);
    return true;
}

// Define a expiração da chave usando um timestamp Unix em segundos (Comando EXPIREAT)
bool TTLOperation::expireAt(string_view key, uint64_t unixTimeSeconds)
{
    if (unixTimeSeconds > UINT64_MAX / 1000)
    {
        return pexpireAt(key, UINT64_MAX);
    }
    return pexpireAt(key, unixTimeSeconds * 1000);
}

// Define a expiração da chave usando um timestamp Unix em milissegundos (Comando PEXPIREAT)
bool TTLOperation::pexpireAt(string_view key, uint64_t unixTimeMs)
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

    if (unixTimeMs <= now)
    {
        table.deleteSlot(idx);
        return true;
    }

    slot.item->expireAtMs = unixTimeMs;
    return true;
}

// Consulta o tempo de vida restante em segundos (Comando TTL):
// Obtém o valor em milissegundos via pttl e converte arredondando para cima,
// assegurando que a chave não retorne 0 enquanto ainda estiver viva.
int64_t TTLOperation::ttl(string_view key)
{
    int64_t ms = pttl(key);
    if (ms < 0)
    {
        return ms; // -1 ou -2
    }
    return (ms + 999) / 1000;
}

// Consulta o tempo de vida restante em milissegundos (Comando PTTL):
// - Retorna -2 se a chave não existir ou tiver expirado (aciona lazy deletion).
// - Retorna -1 se a chave existir mas for permanente (expireAtMs == 0).
// - Retorna os milissegundos restantes (expireAtMs - now).
int64_t TTLOperation::pttl(string_view key)
{
    size_t idx = table.findSlotIndex(key);
    if (idx == table.capacity)
    {
        return -2;
    }

    FlatTable::Slot &slot = table.slots[idx];
    uint64_t now = FlatTable::getCurrentTimeMs();
    if (slot.item->isExpired(now))
    {
        table.deleteSlot(idx);
        return -2;
    }

    if (slot.item->expireAtMs == 0)
    {
        return -1;
    }

    return static_cast<int64_t>(slot.item->expireAtMs - now);
}

// Remove o tempo de vida da chave, tornando-a persistente (Comando PERSIST):
// Retorna true se a chave possuía um timeout e foi resetada; false caso contrário.
bool TTLOperation::persist(string_view key)
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

    if (slot.item->expireAtMs == 0)
    {
        return false;
    }

    slot.item->expireAtMs = 0;
    return true;
}

// Varre ativamente toda a tabela, expurgando em lote todas as chaves cujo tempo expirou.
// Libera os blocos alocados de volta para o SlabAllocator. Retorna o total expurgado.
size_t TTLOperation::purgeExpired()
{
    uint64_t now = FlatTable::getCurrentTimeMs();
    size_t count = 0;
    for (size_t i = 0; i < table.capacity; ++i)
    {
        FlatTable::Slot &slot = table.slots[i];
        if (slot.state == FlatTable::SlotState::OCCUPIED && slot.item != nullptr && slot.item->isExpired(now))
        {
            table.deleteSlot(i);
            ++count;
        }
    }
    return count;
}
