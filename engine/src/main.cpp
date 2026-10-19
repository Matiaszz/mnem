#include <iostream>
#include <cstdint>
#include <new>
#include <cstring>
#include "headers/SlabAllocator.h"
#include "headers/FlatTable.h"

using namespace std;

struct CacheEntry
{
    uint32_t keyHash;
    uint32_t ttl;
    char payload[56]; // Total: 64 bytes
};

int main()
{
    // 1. Slab gerenciando blocos de 128 bytes (capacidade de 10.000 itens)
    SlabAllocator slab128(128, 10000);

    // 2. Flat Table contígua com 16.384 slots (potência de 2 para bitmasking rápido)
    FlatTable cache(16384, slab128);

    // Gravação (SET)
    cache.set("session:token:usr_99", "eyJhbGciOiJIUzI1NiJ9");
    cache.set("metrics:db:query_ms", "14.2");

    // Leitura (GET)
    if (auto val = cache.get("session:token:usr_99"))
    {
        cout << "GET session:token:usr_99 -> " << *val << "\n";
    }

    // Remoção (DEL)
    bool deleted = cache.del("metrics:db:query_ms");
    cout << "DEL metrics:db:query_ms -> " << (deleted ? "OK" : "NOT_FOUND") << "\n";

    // Confirmação de ausência
    if (!cache.get("metrics:db:query_ms"))
    {
        cout << "Chave removida com sucesso (TOMBSTONE processado).\n";
    }

    return 0;
}