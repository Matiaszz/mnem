#pragma once

#include <string>
#include <unordered_map>
#include <atomic>
#include "headers/ft/FlatTable.h"
#include "headers/network/CommandHandler.h"

#ifndef MNEM_SERVER_H
#define MNEM_SERVER_H

// Servidor TCP baseado em epoll para recepção e processamento de conexões de clientes.
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
    struct Connection
    {
        int fd{-1};
        std::string inBuffer;
        std::string outBuffer;
        bool shouldClose{false};
    };

    FlatTable &table;
    CommandHandler commandHandler;
    int port;
    std::string host;

    int serverFd{-1};
    int epollFd{-1};
    std::atomic<bool> running{false};

    std::unordered_map<int, Connection> connections;

    bool initSocket();
    void acceptConnection();
    void handleClientRead(int clientFd);
    void handleClientWrite(int clientFd);
    void closeConnection(int clientFd);
    void setNonBlocking(int fd);
    void flushOutput(Connection &conn);
};

#endif // MNEM_SERVER_H
