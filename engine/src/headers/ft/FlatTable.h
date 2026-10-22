#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>
#include <optional>
#include <chrono>
#include <atomic>
#include "headers/SlabAllocator.h"
#include "headers/ft/FlatTableTypes.h"
#include "headers/ft/ReadOperation.h"
#include "headers/ft/WriteOperation.h"
#include "headers/ft/TTLOperation.h"

#ifndef MNEM_FlatTable_H
#define MNEM_FlatTable_H

// Tabela hash plana de endereçamento aberto (linear probing) de alta performance.
// Atua como fachada unificada delegando para os módulos especializados:
// ReadOperation, WriteOperation e TTLOperation.
class FlatTable
{
public:
    // Estado de cada slot da tabela
    enum class SlotState : uint8_t
    {
        EMPTY = 0,    // Slot nunca ocupado (encerra a cadeia de busca)
        OCCUPIED = 1, // Slot ocupado com um item ativo
        TOMBSTONE = 2 // Slot previamente ocupado e depois removido (permite continuar a busca)
    };

    struct Slot
    {
        uint64_t hash;
        Item *item;
        SlotState state;
        uint8_t padding[7];
    };

private:
    std::vector<Slot> slots;
    size_t capacity;
    size_t mask;
    size_t activeCount;
    SlabAllocator &allocator;
    inline static std::atomic<int64_t> timeOffsetMs{0}; // Deslocamento de tempo para testes determinísticos

    // Submódulos de operações especializadas
    ReadOperation readOp;
    WriteOperation writeOp;
    TTLOperation ttlOp;

    friend class ReadOperation;
    friend class WriteOperation;
    friend class TTLOperation;

public:
    // FNV-1a Hash de 64 bits da chave
    static uint64_t hashKey(std::string_view key)
    {
        uint64_t hash = 14695981039346656037ULL;
        for (char c : key)
        {
            hash ^= static_cast<uint8_t>(c);
            hash *= 1099511628211ULL;
        }
        return hash;
    }

    // Retorna o timestamp atual em milissegundos (Unix epoch + timeOffset de teste)
    static uint64_t getCurrentTimeMs()
    {
        auto realNow = static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch())
                .count());
        return static_cast<uint64_t>(static_cast<int64_t>(realNow) + timeOffsetMs.load());
    }

    // Avança o relógio interno em deltaMs milissegundos (útil para testar expiração)
    static void advanceTimeMs(int64_t deltaMs)
    {
        timeOffsetMs.fetch_add(deltaMs);
    }

    // Restaura o relógio interno para o horário real do sistema
    static void resetTimeOffset()
    {
        timeOffsetMs.store(0);
    }

    // Construtor: inicializa a tabela com capacidade potência de 2 e vincula ao SlabAllocator
    FlatTable(size_t pow2Capacity, SlabAllocator &alloc);

    // ==========================================
    // Operações de Leitura (ReadOperation)
    // ==========================================
    std::optional<ValueView> get(std::string_view key) { return readOp.get(key); }
    bool exists(std::string_view key) { return readOp.exists(key); }
    std::optional<Value> getValueCopy(std::string_view key) { return readOp.getValueCopy(key); }

    // ==========================================
    // Operações de Escrita (WriteOperation)
    // ==========================================
    bool set(std::string_view key, const Value &val, uint64_t expireAtMs = 0) { return writeOp.set(key, val, expireAtMs); }
    bool setEx(std::string_view key, const Value &val, int64_t seconds) { return writeOp.setEx(key, val, seconds); }
    bool setPx(std::string_view key, const Value &val, int64_t milliseconds) { return writeOp.setPx(key, val, milliseconds); }
    bool del(std::string_view key) { return writeOp.del(key); }
    bool rename(std::string_view key, std::string_view newKey, bool renameIfNotExist) { return writeOp.rename(key, newKey, renameIfNotExist); }
    bool replaceKeyAtomically(
        std::string_view key,
        std::string_view newKey,
        const Value &value,
        bool onlyIfDestinationMissing,
        uint64_t expireAtMs = 0)
    {
        return writeOp.replaceKeyAtomically(key, newKey, value, onlyIfDestinationMissing, expireAtMs);
    }

    bool increment(std::string_view key, uint64_t quantity = 1) { return writeOp.increment(key, quantity); }
    bool decrement(std::string_view key, uint64_t quantity = 1) { return writeOp.decrement(key, quantity); }

    // ==========================================
    // Operações de TTL e Expiração (TTLOperation)
    // ==========================================
    bool expire(std::string_view key, int64_t seconds) { return ttlOp.expire(key, seconds); }
    bool pexpire(std::string_view key, int64_t milliseconds) { return ttlOp.pexpire(key, milliseconds); }
    int64_t ttl(std::string_view key) { return ttlOp.ttl(key); }
    int64_t pttl(std::string_view key) { return ttlOp.pttl(key); }
    bool persist(std::string_view key) { return ttlOp.persist(key); }
    bool expireAt(std::string_view key, uint64_t unixTimeSeconds) { return ttlOp.expireAt(key, unixTimeSeconds); }
    bool pexpireAt(std::string_view key, uint64_t unixTimeMs) { return ttlOp.pexpireAt(key, unixTimeMs); }
    size_t purgeExpired() { return ttlOp.purgeExpired(); }

    // Acesso direto aos subsistemas de operações
    ReadOperation &read() { return readOp; }
    WriteOperation &write() { return writeOp; }
    TTLOperation &ttlOps() { return ttlOp; }

    // Métodos utilitários de estado
    size_t getActiveCount() const { return activeCount; }
    size_t getCapacity() const { return capacity; }
    SlotState getSlotState(size_t idx) const { return slots[idx].state; }
    const Slot &getSlot(size_t idx) const { return slots[idx]; }

private:
    // Localiza o índice do slot onde a chave se encontra ou capacity se não encontrada
    size_t findSlotIndex(std::string_view key) const;

    // Desaloca o item da memória slab, marca o slot como TOMBSTONE e decrementa activeCount
    void deleteSlot(size_t index);

    // Aloca um bloco no SlabAllocator e escreve cabeçalho Item + chave + valor contíguos
    Item *writeItem(std::string_view key, std::string_view val, ValueType type = ValueType::STRING, uint64_t expireAtMs = 0);
};

#endif // MNEM_FlatTable_H
