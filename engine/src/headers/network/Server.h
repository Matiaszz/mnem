#pragma once

#include <string>
#include <atomic>
#include "headers/ft/FlatTable.h"
#include "headers/network/CommandHandler.h"
#include "headers/network/ConnectionManager.h"
#include "platform/net_socket.h"

#ifndef MNEM_SERVER_H
#define MNEM_SERVER_H

// Servidor TCP principal: orquestra o socket de escuta, o laço de eventos e a limpeza periódica de TTL.
class Server
{
public:
    explicit Server(FlatTable &tbl, int port = 6379, const std::string &host = "127.0.0.1");
    ~Server();

    // Impede cópia
    Server(const Server &) = delete;
    Server &operator=(const Server &) = delete;

    // Inicializa o socket e entra no laço de eventos
    bool start();

    // Solicita parada graciosa do servidor
    void stop();

    bool isRunning() const { return running.load(); }
    int getPort() const { return port; }
    const std::string &getHost() const { return host; }

private:
    FlatTable &table;
    CommandHandler commandHandler;
    mnem::platform::SocketPoller poller;
    ConnectionManager connectionManager;

    int port;
    std::string host;
    int serverFd{-1};
    std::atomic<bool> running{false};

    void setupSignals();
    void runEventLoop();
};

#endif // MNEM_SERVER_H
