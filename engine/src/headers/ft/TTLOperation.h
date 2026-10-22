#pragma once

#include <string_view>
#include <cstdint>
#include <cstddef>

#ifndef MNEM_TTLOPERATION_H
#define MNEM_TTLOPERATION_H

class FlatTable;

// Classe responsável exclusivamente pelas operações de tempo de vida (TTL/PTTL),
// expiração (EXPIRE/PEXPIRE/EXPIREAT/PEXPIREAT), persistência (PERSIST) e varredura ativa
class TTLOperation
{
private:
    FlatTable &table;

public:
    explicit TTLOperation(FlatTable &tbl);

    // Define tempo de vida (TTL) em segundos para a chave (Comando EXPIRE)
    bool expire(std::string_view key, int64_t seconds);

    // Define tempo de vida (PTTL) em milissegundos para a chave (Comando PEXPIRE)
    bool pexpire(std::string_view key, int64_t milliseconds);

    // Retorna tempo restante em segundos: -2 (não existe/expirou), -1 (sem TTL), >=1 (segundos restantes)
    int64_t ttl(std::string_view key);

    // Retorna tempo restante em milissegundos: -2 (não existe/expirou), -1 (sem TTL), >=0 (ms restantes)
    int64_t pttl(std::string_view key);

    // Remove o timeout da chave tornando-a permanente (Comando PERSIST)
    bool persist(std::string_view key);

    // Define expiração por Unix timestamp em segundos (Comando EXPIREAT)
    bool expireAt(std::string_view key, uint64_t unixTimeSeconds);

    // Define expiração por Unix timestamp em milissegundos (Comando PEXPIREAT)
    bool pexpireAt(std::string_view key, uint64_t unixTimeMs);

    // Varredura ativa na tabela para expurgar chaves expiradas em lote
    size_t purgeExpired();
};

#endif // MNEM_TTLOPERATION_H
