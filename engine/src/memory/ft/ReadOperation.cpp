#include "headers/ft/ReadOperation.h"
#include "headers/ft/FlatTable.h"

using namespace std;

// Construtor: vincula a operação à instância da FlatTable
ReadOperation::ReadOperation(FlatTable &tbl)
    : table(tbl)
{
}

// Busca o valor de uma chave (Comando GET do Redis):
// 1. Procura o slot da chave via sondagem linear (findSlotIndex).
// 2. Se encontrada, verifica se expirou (now >= expireAtMs).
// 3. Se expirada, realiza a expiração passiva (lazy deletion) via deleteSlot e retorna nullopt.
// 4. Se ativa, retorna a ValueView com tipo e bytes correspondentes.
optional<ValueView> ReadOperation::get(string_view key)
{
    size_t idx = table.findSlotIndex(key);
    if (idx == table.capacity)
    {
        return nullopt;
    }

    FlatTable::Slot &slot = table.slots[idx];
    uint64_t now = FlatTable::getCurrentTimeMs();
    if (slot.item->isExpired(now))
    {
        // Expiração passiva (lazy expiration)
        table.deleteSlot(idx);
        return nullopt;
    }

    return ValueView{
        slot.item->type,
        string_view(slot.item->val(), slot.item->valLen)};
}

// Verifica se a chave existe e não está expirada (Comando EXISTS do Redis)
bool ReadOperation::exists(string_view key)
{
    return get(key).has_value();
}

// Retorna uma cópia independente do Value armazenado (se existir e não estiver expirado)
optional<Value> ReadOperation::getValueCopy(string_view key)
{
    auto view = get(key);
    if (!view)
        return nullopt;

    return view->toValue();
}
