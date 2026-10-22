#include <iostream>
#include "headers/SlabAllocator.h"
#include "headers/ft/FlatTable.h"
#include "headers/network/Server.h"
#include <cassert>
#include <cstring>
#include <vector>
#include <string>
#include <thread>
#include <chrono>

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

static void runInternalTests()
{
    cout << "========================================\n";
    cout << "TESTES DE REDIMENSIONAMENTO NO SLABALLOCATOR\n";
    cout << "========================================\n";

    {
        // 1. Cria um SlabAllocator com capacidade inicial pequena de apenas 3 blocos
        cout << "[Slab 1] Inicializando SlabAllocator com capacidade de 3 blocos...\n";
        SlabAllocator slab(128, 3, 2);
        assert(slab.getTotalBlocks() == 3);
        assert(slab.getAllocatedBlocks() == 0);
        assert(slab.getFreeBlocks() == 3);
        assert(slab.getChunkCount() == 1);

        // Aloca os 3 blocos iniciais
        void *p1 = slab.allocate();
        void *p2 = slab.allocate();
        void *p3 = slab.allocate();
        assert(p1 && p2 && p3);
        assert(slab.getAllocatedBlocks() == 3);
        assert(slab.getFreeBlocks() == 0);
        assert(slab.getChunkCount() == 1);
        cout << "  -> 3 blocos alocados. Capacidade esgotada (freeBlocks = 0).\n";

        // 2. Alocação além da capacidade: dispara estratégia de redimensionamento automático
        cout << "[Slab 2] Alocando 4º bloco (dispara redimensionamento automático: 3 * 2 = 6 novos blocos)...\n";
        void *p4 = slab.allocate();
        assert(p4 != nullptr);
        assert(slab.getChunkCount() == 2);
        assert(slab.getTotalBlocks() == 9); // 3 + 6 = 9
        assert(slab.getAllocatedBlocks() == 4);
        assert(slab.getFreeBlocks() == 5);
        cout << "  -> 4º bloco alocado! totalBlocks expandido para: " << slab.getTotalBlocks()
             << ", Chunks ativos: " << slab.getChunkCount() << "\n";

        // 3. Valida escrita e isolamento de memória nos diferentes chunks
        memset(p1, 0xAA, 128);
        memset(p4, 0xBB, 128);
        assert(static_cast<uint8_t *>(p1)[0] == 0xAA);
        assert(static_cast<uint8_t *>(p4)[0] == 0xBB);

        // 4. Validação de índices globais
        size_t idx1 = slab.getPointerIndex(p1);
        size_t idx4 = slab.getPointerIndex(p4);
        assert(idx1 < 3);
        assert(idx4 >= 3 && idx4 < 9);

        // 5. Desalocação entre múltiplos chunks e proteção contra double-free
        assert(slab.deallocate(p4));
        assert(slab.getAllocatedBlocks() == 3);
        assert(slab.getFreeBlocks() == 6);
        assert(!slab.deallocate(p4)); // double-free recusado

        assert(slab.deallocate(p1));
        assert(slab.deallocate(p2));
        assert(slab.deallocate(p3));
        assert(slab.getAllocatedBlocks() == 0);
        assert(slab.getFreeBlocks() == 9);

        // 6. Expansão manual via grow()
        assert(slab.grow(10));
        assert(slab.getTotalBlocks() == 19);
        assert(slab.getChunkCount() == 3);
        cout << "  -> Expansão manual via grow(10) validada. totalBlocks: " << slab.getTotalBlocks() << "\n";
    }

    cout << "\n========================================\n";
    cout << "TESTE DE FLATTABLE SEM PASSAR SLABALLOCATOR\n";
    cout << "========================================\n";

    // Tabela autônoma com capacidade 8 e blocos de 128 bytes (SEM passar SlabAllocator!)
    const size_t CAP = 8;
    FlatTable table(CAP, 128);

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

    // 6. Preencher todos os slots e verificar comportamento de tabela cheia (sem passar allocator!)
    cout << "\n[6] Teste de tabela 100% cheia (sem passar SlabAllocator)...\n";
    const size_t MINI_CAP = 4;
    FlatTable fullTable(MINI_CAP, 128);

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

    // =========================================================================
    // TESTES DE EXPIRE, PEXPIRE E TTL (SEM PASSAR SLABALLOCATOR)
    // =========================================================================
    cout << "\n========================================\n";
    cout << "TESTES DE EXPIRE, PEXPIRE E TTL (SEM PASSAR SLABALLOCATOR)\n";
    cout << "========================================\n";

    FlatTable expTable(16);
    FlatTable::resetTimeOffset();

    // [7] TTL de chave inexistente e chave sem expiração
    cout << "\n[7] Verificando TTL/PTTL para chave inexistente e sem expiração...\n";
    assert(expTable.ttl("nao_existe") == -2);
    assert(expTable.pttl("nao_existe") == -2);

    assert(expTable.set("persistente", "dados_duradouros"));
    assert(expTable.ttl("persistente") == -1); // Sem timeout associado
    assert(expTable.pttl("persistente") == -1);
    cout << "  -> Chave inexistente retorna TTL = -2.\n";
    cout << "  -> Chave sem timeout retorna TTL = -1.\n";

    // [8] EXPIRE e TTL (em segundos)
    cout << "\n[8] Testando comando EXPIRE e contagem regressiva de TTL...\n";
    assert(expTable.set("sessao", "token_abc"));
    assert(expTable.expire("sessao", 10)); // Expira em 10 segundos
    assert(expTable.ttl("sessao") == 10);
    assert(expTable.pttl("sessao") > 9000 && expTable.pttl("sessao") <= 10000);
    cout << "  -> EXPIRE 'sessao' 10 definido. TTL inicial: " << expTable.ttl("sessao") << "s\n";

    // Avança 4 segundos no relógio
    FlatTable::advanceTimeMs(4000);
    assert(expTable.ttl("sessao") == 6);
    assert(expTable.get("sessao").has_value());
    assert(expTable.get("sessao")->asString() == "token_abc");
    cout << "  -> Após avançar 4s: TTL restante = " << expTable.ttl("sessao") << "s. Chave ainda válida.\n";

    // Avança mais 7 segundos (total 11s decorridos > 10s timeout)
    FlatTable::advanceTimeMs(7000);
    assert(expTable.ttl("sessao") == -2);        // Expirou!
    assert(!expTable.get("sessao").has_value()); // Lazy deletion acionada no get
    assert(!expTable.exists("sessao"));
    assert(!expTable.del("sessao")); // Del em chave expirada retorna false
    cout << "  -> Após expirar (11s decorridos): TTL = " << expTable.ttl("sessao")
         << ", GET = nullopt, EXISTS = false, ativação removida da tabela.\n";

    // [9] PEXPIRE e PTTL (em milissegundos) com teste em tempo real
    cout << "\n[9] Testando comando PEXPIRE e PTTL (milissegundos) em tempo real...\n";
    FlatTable::resetTimeOffset();
    assert(expTable.set("cache_rapido", "valor_temporario"));
    assert(expTable.pexpire("cache_rapido", 150)); // 150 ms
    int64_t initialPttl = expTable.pttl("cache_rapido");
    assert(initialPttl > 0 && initialPttl <= 150);
    cout << "  -> PEXPIRE definido com 150ms. PTTL inicial: " << initialPttl << "ms\n";

    // Pequena pausa na thread para medir tempo real decorrido
    this_thread::sleep_for(chrono::milliseconds(25));
    int64_t remainingPttl = expTable.pttl("cache_rapido");
    assert(remainingPttl < initialPttl);
    assert(remainingPttl > 0);
    cout << "  -> Após sleep de 25ms: PTTL reduziu para " << remainingPttl << "ms.\n";

    // Aguarda expiração completa em tempo real
    this_thread::sleep_for(chrono::milliseconds(140));
    assert(expTable.pttl("cache_rapido") == -2);
    assert(expTable.ttl("cache_rapido") == -2);
    assert(!expTable.get("cache_rapido").has_value());
    cout << "  -> Após expiração em tempo real: PTTL = -2 e chave coletada com sucesso.\n";

    // [10] Casos de borda: EXPIRE e PEXPIRE com tempo <= 0 (deleção imediata)
    cout << "\n[10] Testando EXPIRE/PEXPIRE com timeout <= 0 (deleção imediata)...\n";
    assert(expTable.set("imediato_1", "sera_deletado"));
    assert(expTable.expire("imediato_1", 0)); // TTL <= 0 deleta imediatamente e retorna true
    assert(!expTable.exists("imediato_1"));
    assert(expTable.ttl("imediato_1") == -2);

    assert(expTable.set("imediato_2", "sera_deletado_negativo"));
    assert(expTable.pexpire("imediato_2", -50)); // PTTL < 0 deleta imediatamente
    assert(!expTable.exists("imediato_2"));
    assert(expTable.ttl("imediato_2") == -2);

    // Chamar em chave inexistente retorna false
    assert(!expTable.expire("nao_existe_para_expire", 5));
    assert(!expTable.pexpire("nao_existe_para_pexpire", 500));
    cout << "  -> EXPIRE <= 0 e PEXPIRE <= 0 excluem chaves existentes e retornam true.\n";
    cout << "  -> EXPIRE/PEXPIRE em chaves inexistentes retornam false.\n";

    // [11] Comando PERSIST
    cout << "\n[11] Testando comando PERSIST (remover timeout de chave)...\n";
    assert(expTable.set("chave_com_timeout", "dado"));
    assert(expTable.expire("chave_com_timeout", 50));
    assert(expTable.ttl("chave_com_timeout") == 50);

    assert(expTable.persist("chave_com_timeout"));   // Remove TTL
    assert(expTable.ttl("chave_com_timeout") == -1); // Volta a ser persistente
    assert(!expTable.persist("chave_com_timeout"));  // Segunda chamada retorna false (já não tem TTL)
    assert(!expTable.persist("chave_inexistente"));  // Chave inexistente retorna false

    FlatTable::advanceTimeMs(60000);              // Avança 60 segundos
    assert(expTable.exists("chave_com_timeout")); // Continua existindo pois TTL foi removido!
    assert(expTable.get("chave_com_timeout")->asString() == "dado");
    cout << "  -> PERSIST removeu o TTL com sucesso; chave permanece intacta após o timeout original.\n";

    // [12] Sobrescrita com SET limpa o TTL antigo
    cout << "\n[12] Verificando que sobrescrever com SET limpa o TTL anterior...\n";
    assert(expTable.set("chave_sobrescrita", "antigo"));
    assert(expTable.expire("chave_sobrescrita", 20));
    assert(expTable.ttl("chave_sobrescrita") == 20);

    assert(expTable.set("chave_sobrescrita", "novo_sem_ttl"));
    assert(expTable.ttl("chave_sobrescrita") == -1); // TTL limpado!
    assert(expTable.get("chave_sobrescrita")->asString() == "novo_sem_ttl");
    cout << "  -> SET removeu o TTL anterior e atualizou o valor corretamente.\n";

    // [13] SETEX e SETPX (atalhos atômicos com expiração)
    cout << "\n[13] Testando SETEX e SETPX...\n";
    assert(expTable.setEx("chave_setex", "valor_setex", 3));
    assert(expTable.ttl("chave_setex") == 3);

    assert(expTable.setPx("chave_setpx", "valor_setpx", 2500));
    assert(expTable.pttl("chave_setpx") > 2000 && expTable.pttl("chave_setpx") <= 2500);
    cout << "  -> SETEX e SETPX configurados com valores e TTLs atômicos.\n";

    // [14] RENAME preservando TTL
    cout << "\n[14] Testando RENAME com preservação de TTL...\n";
    assert(expTable.set("origem", "dados_migracao"));
    assert(expTable.expire("origem", 30));
    FlatTable::advanceTimeMs(10000); // Passam 10 segundos (restando 20)
    assert(expTable.ttl("origem") == 20);

    assert(expTable.rename("origem", "destino", false));
    assert(!expTable.exists("origem"));
    assert(expTable.exists("destino"));
    assert(expTable.ttl("destino") == 20); // TTL transferido para o novo nome!
    assert(expTable.get("destino")->asString() == "dados_migracao");
    cout << "  -> RENAME transferiu chave e preservou TTL restante (" << expTable.ttl("destino") << "s) com perfeição.\n";

    // [15] Varredura ativa: purgeExpired()
    cout << "\n[15] Testando limpeza ativa em lote com purgeExpired()...\n";
    FlatTable purgeTable(16);
    assert(purgeTable.set("exp_1", "a"));
    assert(purgeTable.set("exp_2", "b"));
    assert(purgeTable.set("exp_3", "c"));
    assert(purgeTable.set("permanente_1", "d"));

    assert(purgeTable.expire("exp_1", 2));
    assert(purgeTable.expire("exp_2", 2));
    assert(purgeTable.expire("exp_3", 2));

    assert(purgeTable.getActiveCount() == 4);
    FlatTable::advanceTimeMs(5000); // 5 segundos passam, exp_1, exp_2, exp_3 expiram

    // Purge varre a tabela ativamente sem precisar de GET prévio
    size_t purgedCount = purgeTable.purgeExpired();
    assert(purgedCount == 3);
    assert(purgeTable.getActiveCount() == 1);
    assert(purgeTable.exists("permanente_1"));
    assert(!purgeTable.exists("exp_1"));
    assert(!purgeTable.exists("exp_2"));
    assert(!purgeTable.exists("exp_3"));
    cout << "  -> purgeExpired() coletou com sucesso 3 chaves expiradas em lote. activeCount atual: "
         << purgeTable.getActiveCount() << "\n";

    FlatTable incDecTable(16);
    assert(incDecTable.set("incdec_1", 1));
    assert(incDecTable.set("incdec_2", 3));
    assert(incDecTable.increment("incdec_1"));
    assert(incDecTable.decrement("incdec_2", 2));

    assert(incDecTable.get("incdec_1")->asInt() == 2);
    assert(incDecTable.get("incdec_2")->asInt() == 1);

    cout << "\n=======================================================\n";
    cout << "TODOS OS TESTES (SLAB REDIMENSIONAMENTO + FLATTABLE AUTÔNOMA) PASSARAM!\n";
    cout << "=======================================================\n";
}

int main(int argc, char *argv[])
{
    if (argc > 1 && string(argv[1]) == "--test")
    {
        runInternalTests();
        return 0;
    }

    int port = 6379;
    string host = "127.0.0.1";

    if (argc > 1)
    {
        try
        {
            port = stoi(argv[1]);
        }
        catch (...)
        {
            cerr << "[Main] Porta informada (" << argv[1] << ") invalida, utilizando padrao " << port << ".\n";
        }
    }

    if (argc > 2)
    {
        host = argv[2];
    }

    FlatTable table(1024, 256);
    Server server(table, port, host);

    if (!server.start())
    {
        cerr << "[Main] Erro ao iniciar servidor no endereco " << host << ":" << port << "\n";
        return 1;
    }

    return 0;
}
