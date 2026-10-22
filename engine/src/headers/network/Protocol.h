#pragma once

#include <string>
#include <string_view>
#include <vector>
#include <cstdint>

#ifndef MNEM_PROTOCOL_H
#define MNEM_PROTOCOL_H

// Protocolo de comunicação e serialização de comandos para o servidor de armazenamento em memória.
class Protocol
{
public:
    enum class ParseStatus
    {
        OK,
        INCOMPLETE,
        ERROR
    };

    // Tenta analisar o próximo comando do buffer de entrada (suporta tanto comando de linha/inline quanto formato em array).
    // Se bem-sucedido, preenche 'tokens' e avança 'consumedBytes'.
    static ParseStatus parseCommand(std::string_view input, std::vector<std::string> &tokens, size_t &consumedBytes);

    // Formatadores de resposta do protocolo:
    static std::string formatSimpleString(std::string_view str);
    static std::string formatError(std::string_view err);
    static std::string formatInteger(int64_t val);
    static std::string formatBulkString(std::string_view val);
    static std::string formatNull();
    static std::string formatArray(const std::vector<std::string> &elements);

private:
    static ParseStatus parseInlineCommand(std::string_view input, std::vector<std::string> &tokens, size_t &consumedBytes);
    static ParseStatus parseArrayCommand(std::string_view input, std::vector<std::string> &tokens, size_t &consumedBytes);
};

#endif // MNEM_PROTOCOL_H
