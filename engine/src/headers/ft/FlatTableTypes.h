#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>
#include <optional>
#include <variant>
#include <ostream>
#include <cstring>

#ifndef MNEM_FLATTABLETYPES_H
#define MNEM_FLATTABLETYPES_H

// Tipos de dados suportados para os valores armazenados
enum class ValueType : std::uint8_t
{
    STRING,
    INTEGER,
    FLOAT,
    JSON,
    BINARY
};

// Estrutura de cabeçalho do item alocado no SlabAllocator.
// Os bytes da chave e do valor ficam contíguos na memória imediatamente após este cabeçalho.
struct alignas(8) Item
{
    uint16_t keyLen;      // Tamanho da chave em bytes
    uint16_t valLen;      // Tamanho do valor em bytes
    ValueType type;       // Tipo do dado armazenado
    uint8_t padding[3];   // Alinhamento para múltiplos de 8 bytes
    uint64_t expireAtMs;  // Timestamp de expiração em milissegundos (0 = sem expiração / infinito)

    // Retorna ponteiro para o início da chave na memória
    const char *key() const;

    // Retorna ponteiro para o início do valor na memória
    const char *val() const;

    // Compara a chave do item com uma string_view
    bool matches(std::string_view k) const;

    // Verifica se o item já expirou com base no tempo atual (nowMs)
    bool isExpired(uint64_t nowMs) const
    {
        return expireAtMs != 0 && nowMs >= expireAtMs;
    }
};

static_assert(sizeof(Item) == 16, "Item size must be 16 bytes");

using Binary = std::vector<std::uint8_t>;

struct Json
{
    std::string_view data;
};

// Variante que encapsula qualquer um dos tipos de valores suportados
using Value = std::variant<
    std::int64_t,
    double,
    std::string_view,
    Binary,
    Json>;

// Visão leve e não proprietária dos dados do valor (zero-copy)
struct ValueView
{
    ValueType type;
    std::string_view bytes;

    // Interpreta os bytes como inteiro de 64 bits se o tipo for compatível
    std::optional<std::int64_t> asInt() const
    {
        if (type == ValueType::INTEGER && bytes.size() == sizeof(std::int64_t))
        {
            std::int64_t v;
            std::memcpy(&v, bytes.data(), sizeof(v));
            return v;
        }
        return std::nullopt;
    }

    // Interpreta os bytes como double se o tipo for compatível
    std::optional<double> asFloat() const
    {
        if (type == ValueType::FLOAT && bytes.size() == sizeof(double))
        {
            double v;
            std::memcpy(&v, bytes.data(), sizeof(v));
            return v;
        }
        return std::nullopt;
    }

    // Retorna a representação textual dos bytes
    std::string_view asString() const
    {
        return bytes;
    }

    // Converte a ValueView de volta para a variante Value completa
    std::optional<Value> toValue() const
    {
        switch (type)
        {
        case ValueType::INTEGER:
            if (auto v = asInt())
                return Value{*v};
            break;

        case ValueType::FLOAT:
            if (auto v = asFloat())
                return Value{*v};
            break;

        case ValueType::STRING:
            return Value{bytes};

        case ValueType::JSON:
            return Value{Json{bytes}};

        case ValueType::BINARY:
            return Value{Binary(bytes.begin(), bytes.end())};
        }

        return std::nullopt;
    }
};

// Sobrecarga de operador para impressão facilitada da ValueView
inline std::ostream &operator<<(std::ostream &os, const ValueView &vv)
{
    switch (vv.type)
    {
    case ValueType::INTEGER:
        if (auto v = vv.asInt())
            return os << *v;
        break;
    case ValueType::FLOAT:
        if (auto v = vv.asFloat())
            return os << *v;
        break;
    default:
        break;
    }
    return os << vv.bytes;
}

#endif // MNEM_FLATTABLETYPES_H
