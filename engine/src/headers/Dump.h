#pragma once

#include <string>

#ifndef MNEM_DUMP_H
#define MNEM_DUMP_H

class FlatTable;

// Mecanismo de persistência por snapshot (Dump) da tabela em memória.
// Salva todos os itens ativos em disco e restaura o estado na inicialização.
class Dump
{
private:
    std::string filePath;

public:
    explicit Dump(const std::string &path = "dump.mnem");
    ~Dump() = default;

    // Gera o arquivo de dump com todos os itens ativos (não expirados) da tabela
    bool save(const FlatTable &table) const;

    // Carrega os itens do arquivo de dump para a tabela (ignorando itens já expirados)
    bool load(FlatTable &table) const;

    const std::string &getFilePath() const { return filePath; }
};

#endif // MNEM_DUMP_H
