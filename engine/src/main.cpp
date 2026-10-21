#include <iostream>
#include "headers/SlabAllocator.h"
#include "headers/FlatTable.h"
#include <cassert>
#include <cstring>
#include <vector>
#include <string>

using namespace std;

// Encontra chaves que colidem no mesmo índice inicial para uma dada capacidade
vector<string> findCollidingKeys(size_t capacity, size_t targetIdx, size_t count)
{
    vector<string> keys;
    size_t mask = capacity - 1;
    for (int i = 0; keys.size() < count; ++i)
    {
        string candidate = "colliding_key_" + to_string(i);
        if ((FlatTable::hashKey(candidate) & mask) == targetIdx)
        {
            keys.push_back(candidate);
        }
    }
    return keys;
}

int main()
{
    cout << "========================================\n";
    cout << "TESTE DE COLISÕES, TOMBSTONES E CAPACIDADE\n";
    cout << "========================================\n";

    // Slab com blocos de 128 bytes
    SlabAllocator slab(128, 100);

    // Tabela com capacidade 8 (slots 0 a 7, máscara 7)
    const size_t CAP = 8;
    FlatTable table(CAP, slab);

    const size_t TARGET_SLOT = 2;
    vector<string> colKeys = findCollidingKeys(CAP, TARGET_SLOT, 4);
    string k1 = colKeys[0];
    string k2 = colKeys[1];
    string k3 = colKeys[2];
    string k4 = colKeys[3];

    cout << "Chaves geradas para colidir no índice " << TARGET_SLOT << ":\n";
    cout << "  k1: " << k1 << " (hash & mask = " << (FlatTable::hashKey(k1) & (CAP - 1)) << ")\n";
    cout << "  k2: " << k2 << " (hash & mask = " << (FlatTable::hashKey(k2) & (CAP - 1)) << ")\n";
    cout << "  k3: " << k3 << " (hash & mask = " << (FlatTable::hashKey(k3) & (CAP - 1)) << ")\n";
    cout << "  k4: " << k4 << " (hash & mask = " << (FlatTable::hashKey(k4) & (CAP - 1)) << ")\n\n";

    // 1. Inserir três chaves em colisão
    cout << "[1] Inserindo 3 chaves com mesmo índice inicial...\n";
    assert(table.set(k1, "valor_1"));
    assert(table.set(k2, "valor_2"));
    assert(table.set(k3, "valor_3"));
    assert(table.getActiveCount() == 3);

    assert(table.getSlotState(2) == FlatTable::SlotState::OCCUPIED);
    assert(table.getSlotState(3) == FlatTable::SlotState::OCCUPIED);
    assert(table.getSlotState(4) == FlatTable::SlotState::OCCUPIED);
    cout << "  -> Inseridas: slot[2]=k1, slot[3]=k2, slot[4]=k3 (activeCount: " << table.getActiveCount() << ")\n";

    // 2. Excluir a primeira (k1 no slot 2)
    cout << "\n[2] Excluindo a primeira chave (k1)...\n";
    assert(table.del(k1));
    assert(table.getActiveCount() == 2);
    assert(table.getSlotState(2) == FlatTable::SlotState::TOMBSTONE);
    cout << "  -> k1 excluída com sucesso. slot[2] agora é TOMBSTONE (activeCount: " << table.getActiveCount() << ")\n";

    // 3. Recuperar a segunda e a terceira (k2 e k3) através do TOMBSTONE
    cout << "\n[3] Recuperando k2 e k3 passando pelo TOMBSTONE do slot[2]...\n";
    auto v2 = table.get(k2);
    auto v3 = table.get(k3);
    assert(v2.has_value());
    assert(v2->asString() == "valor_2");
    assert(v3.has_value());
    assert(v3->asString() == "valor_3");
    cout << "  -> Sucesso! k2 recuperada: \"" << *v2 << "\"\n";
    cout << "  -> Sucesso! k3 recuperada: \"" << *v3 << "\"\n";
    cout << "  -> Conclusão: TOMBSTONE no slot[2] NÃO encerrou a cadeia de sondagem!\n";

    // 4. Inserir uma quarta chave (k4) e verificar reutilização do tombstone
    cout << "\n[4] Inserindo k4 (mesmo hash inicial) e verificando reutilização do TOMBSTONE...\n";
    assert(table.set(k4, "valor_4"));
    assert(table.getActiveCount() == 3);
    assert(table.getSlotState(2) == FlatTable::SlotState::OCCUPIED);
    cout << "  -> k4 inserida. slot[2] reutilizado com sucesso (estado: OCCUPIED, activeCount: " << table.getActiveCount() << ")\n";

    // Verifica que todas as 3 chaves ativas (k4, k2, k3) continuam acessíveis
    assert(table.get(k4)->asString() == "valor_4");
    assert(table.get(k2)->asString() == "valor_2");
    assert(table.get(k3)->asString() == "valor_3");
    cout << "  -> k4, k2 e k3 acessíveis simultaneamente após reutilização do tombstone.\n";

    // 5. Atualizar uma chave existente sem incrementar activeCount
    cout << "\n[5] Atualizando chave existente k2 sem alterar activeCount...\n";
    size_t countBefore = table.getActiveCount();
    assert(table.set(k2, "valor_2_atualizado"));
    size_t countAfter = table.getActiveCount();
    assert(countBefore == countAfter);
    assert(table.get(k2)->asString() == "valor_2_atualizado");
    cout << "  -> k2 atualizada para \"" << *table.get(k2) << "\". activeCount manteve-se em " << countAfter << ".\n";

    // 6. Preencher todos os slots e verificar comportamento de tabela cheia
    cout << "\n[6] Teste de tabela 100% cheia...\n";
    SlabAllocator miniSlab(128, 50);
    const size_t MINI_CAP = 4;
    FlatTable fullTable(MINI_CAP, miniSlab);

    cout << "  -> Criando tabela com capacidade 4 e preenchendo todos os 4 slots...\n";
    assert(fullTable.set("item_0", 100));
    assert(fullTable.set("item_1", 200));
    assert(fullTable.set("item_2", 300));
    assert(fullTable.set("item_3", 400));
    assert(fullTable.getActiveCount() == 4);

    cout << "  -> Tabela cheia (activeCount = 4/4). Tentando inserir 5º item (overflow)...\n";
    bool insertedOverflow = fullTable.set("item_overflow", 999);
    assert(!insertedOverflow);
    assert(fullTable.getActiveCount() == 4);
    assert(!fullTable.get("item_overflow").has_value());
    cout << "  -> Inserção recusada com sucesso (retornou false). activeCount inalterado (4).\n";

    // Confirma integridade dos dados existentes
    assert(fullTable.get("item_0")->asInt() == 100);
    assert(fullTable.get("item_1")->asInt() == 200);
    assert(fullTable.get("item_2")->asInt() == 300);
    assert(fullTable.get("item_3")->asInt() == 400);
    cout << "  -> Todos os 4 itens originais permanecem íntegros.\n";

    // Busca por chave inexistente em tabela cheia não entra em loop infinito
    cout << "  -> Buscando chave inexistente em tabela cheia...\n";
    assert(!fullTable.get("chave_fantasma").has_value());
    assert(!fullTable.del("chave_fantasma"));
    cout << "  -> Busca e exclusão de chave inexistente retornaram false/nullopt sem entrar em loop infinito.\n";

    // Exclui um item para abrir um tombstone na tabela cheia
    cout << "  -> Excluindo item_1 (abre tombstone)...\n";
    assert(fullTable.del("item_1"));
    assert(fullTable.getActiveCount() == 3);

    // Agora a inserção deve conseguir reutilizar o tombstone
    cout << "  -> Inserindo item_overflow no tombstone recém-aberto...\n";
    assert(fullTable.set("item_overflow", 999));
    assert(fullTable.getActiveCount() == 4);
    assert(fullTable.get("item_overflow")->asInt() == 999);
    cout << "  -> Sucesso! item_overflow inserido reaproveitando o tombstone.\n";

    cout << "\n========================================\n";
    cout << "TODOS OS ASSERTIONS PASSARAM COM SUCESSO!\n";
    cout << "========================================\n";

    return 0;
}
