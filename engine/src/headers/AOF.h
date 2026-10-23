#pragma once

#include <string>
#include <string_view>
#include <fstream>

#ifndef MNEM_AOF_H
#define MNEM_AOF_H

// Mecanismo de persistência baseado em arquivo de log sequencial (Append-Only File).
// Registra operações de escrita em disco para durabilidade e reconstrução de estado.
class AOF
{
private:
    std::string filePath;
    std::ofstream fileStream;
    bool enabled{true};

public:
    explicit AOF(const std::string &path = "appendonly.aof");
    ~AOF();

    // Impede cópia
    AOF(const AOF &) = delete;
    AOF &operator=(const AOF &) = delete;

    // Movimentação permitida
    AOF(AOF &&other) noexcept;
    AOF &operator=(AOF &&other) noexcept;

    // Acrescenta uma operação (ex: "SET chave valor") ao final do arquivo
    bool append(std::string_view operation);

    // Limpa todo o conteúdo do arquivo AOF
    bool clear();

    void setEnabled(bool state) { enabled = state; }
    bool isEnabled() const { return enabled; }
    const std::string &getFilePath() const { return filePath; }
};

#endif // MNEM_AOF_H
