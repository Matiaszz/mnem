#pragma once

#include <string>
#include <unordered_map>
#include "headers/network/CommandHandler.h"
#include "platform/net_socket.h"

#ifndef MNEM_CONNECTIONMANAGER_H
#define MNEM_CONNECTIONMANAGER_H

// Gerencia o ciclo de vida, buffers de leitura/escrita e execução de comandos das conexões de clientes.
class ConnectionManager
{
public:
    struct Connection
    {
        int fd{-1};
        std::string inBuffer;
        std::string outBuffer;
        bool shouldClose{false};
    };

    ConnectionManager(CommandHandler &handler, mnem::platform::SocketPoller &poller);
    ~ConnectionManager();

    // Aceita todas as conexões pendentes no socket de escuta
    void acceptNewConnections(int serverFd);

    // Lê dados recebidos de um cliente, analisa o protocolo e executa os comandos
    void handleClientRead(int clientFd);

    // Descarrega dados pendentes no buffer de saída do cliente
    void handleClientWrite(int clientFd);

    // Encerra e remove uma conexão específica
    void closeConnection(int clientFd);

    // Encerra e limpa todas as conexões ativas
    void closeAll();

    bool hasConnection(int clientFd) const { return connections.find(clientFd) != connections.end(); }
    size_t getActiveConnectionCount() const { return connections.size(); }

private:
    CommandHandler &commandHandler;
    mnem::platform::SocketPoller &poller;
    std::unordered_map<int, Connection> connections;

    void processInputBuffer(Connection &conn);
    void flushOutput(Connection &conn);
};

#endif // MNEM_CONNECTIONMANAGER_H
