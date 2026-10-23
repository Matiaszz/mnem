#pragma once

#include <string>
#include <vector>
#include "headers/ft/FlatTable.h"

#ifndef MNEM_COMMANDHANDLER_H
#define MNEM_COMMANDHANDLER_H

// Despachador e executor de comandos do banco de dados em memória.
class CommandHandler
{
public:
    explicit CommandHandler(FlatTable &tbl);

    // Executa uma lista de tokens de comando e retorna a resposta formatada pelo protocolo.
    // Sinaliza shouldClose = true caso o comando seja QUIT/EXIT ou encerre a sessão.
    std::string execute(const std::vector<std::string> &tokens, bool &shouldClose);

    // Lê e reexecuta os comandos pendentes no arquivo AOF sem regravar no próprio AOF
    size_t replayAof();

private:
    FlatTable &table;

    std::string handlePing(const std::vector<std::string> &tokens);
    std::string handleSet(const std::vector<std::string> &tokens);
    std::string handleGet(const std::vector<std::string> &tokens);
    std::string handleDel(const std::vector<std::string> &tokens);
    std::string handleExists(const std::vector<std::string> &tokens);
    std::string handleIncr(const std::vector<std::string> &tokens);
    std::string handleIncrBy(const std::vector<std::string> &tokens);
    std::string handleDecr(const std::vector<std::string> &tokens);
    std::string handleDecrBy(const std::vector<std::string> &tokens);
    std::string handleExpire(const std::vector<std::string> &tokens);
    std::string handlePExpire(const std::vector<std::string> &tokens);
    std::string handleTtl(const std::vector<std::string> &tokens);
    std::string handlePTtl(const std::vector<std::string> &tokens);
    std::string handlePersist(const std::vector<std::string> &tokens);
    std::string handleRename(const std::vector<std::string> &tokens);
    std::string handleRenameNx(const std::vector<std::string> &tokens);
    std::string handlePurge(const std::vector<std::string> &tokens);
    std::string handleDump(const std::vector<std::string> &tokens);
    std::string handleDbSize(const std::vector<std::string> &tokens);
    std::string handleInfo(const std::vector<std::string> &tokens);
    std::string handleHelp(const std::vector<std::string> &tokens);

    static std::string toUpper(std::string str);
};

#endif // MNEM_COMMANDHANDLER_H
