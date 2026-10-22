#pragma once

#include <string_view>
#include <cstdint>
#include "headers/ft/FlatTableTypes.h"

#ifndef MNEM_WRITEOPERATION_H
#define MNEM_WRITEOPERATION_H

class FlatTable;

// Classe responsável exclusivamente pelas operações de escrita, atualização, exclusão e renomeação
class WriteOperation
{
private:
    FlatTable &table;

public:
    explicit WriteOperation(FlatTable &tbl);

    // Insere ou atualiza uma chave com um valor e opcionalmente um timestamp de expiração (em ms).
    // Nota: ao atualizar uma chave existente sem informar expireAtMs, qualquer TTL prévio é resetado (padrão Redis).
    bool set(std::string_view key, const Value &val, uint64_t expireAtMs = 0);

    // Define chave com expiração em segundos de forma atômica (estilo Redis SETEX)
    bool setEx(std::string_view key, const Value &val, int64_t seconds);

    // Define chave com expiração em milissegundos de forma atômica (estilo Redis PSETEX)
    bool setPx(std::string_view key, const Value &val, int64_t milliseconds);

    // Remove uma chave da tabela e devolve a memória ao SlabAllocator
    bool del(std::string_view key);

    // Renomeia 'key' para 'newKey', preservando tanto o valor quanto o TTL restante
    bool rename(std::string_view key, std::string_view newKey, bool renameIfNotExist);

    // Substitui a chave de forma atômica preservando valor e TTL
    bool replaceKeyAtomically(
        std::string_view key,
        std::string_view newKey,
        const Value &value,
        bool onlyIfDestinationMissing,
        uint64_t expireAtMs = 0);

    bool increment(std::string_view key, uint64_t quantity = 1);
    bool decrement(std::string_view key, uint64_t quantity = 1);
};

#endif // MNEM_WRITEOPERATION_H
