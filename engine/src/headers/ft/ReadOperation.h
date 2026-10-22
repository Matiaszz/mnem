#pragma once

#include <string_view>
#include <optional>
#include "headers/ft/FlatTableTypes.h"

#ifndef MNEM_READOPERATION_H
#define MNEM_READOPERATION_H

class FlatTable;

// Classe responsável exclusivamente pelas operações de leitura e consulta na FlatTable
class ReadOperation
{
private:
    FlatTable &table;

public:
    explicit ReadOperation(FlatTable &tbl);

    // Busca o valor associado a uma chave. Se a chave estiver expirada, aciona a lazy deletion e retorna nullopt.
    std::optional<ValueView> get(std::string_view key);

    // Verifica se a chave existe e ainda não expirou
    bool exists(std::string_view key);

    // Retorna uma cópia independente do Value armazenado
    std::optional<Value> getValueCopy(std::string_view key);
};

#endif // MNEM_READOPERATION_H
