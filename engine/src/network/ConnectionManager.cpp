#include "headers/network/ConnectionManager.h"
#include "headers/network/Protocol.h"
#include <vector>

using namespace std;
using namespace mnem::platform;

ConnectionManager::ConnectionManager(CommandHandler &handler, SocketPoller &poller)
    : commandHandler(handler),
      poller(poller)
{
}

ConnectionManager::~ConnectionManager()
{
    closeAll();
}

void ConnectionManager::acceptNewConnections(int serverFd)
{
    int clientFd = -1;
    while (accept_client(serverFd, clientFd))
    {
        if (!poller.addClient(clientFd))
        {
            close_socket(clientFd);
            continue;
        }

        Connection conn;
        conn.fd = clientFd;
        connections[clientFd] = std::move(conn);
    }
}

void ConnectionManager::handleClientRead(int clientFd)
{
    auto it = connections.find(clientFd);
    if (it == connections.end())
    {
        return;
    }

    Connection &conn = it->second;
    char buffer[4096];

    while (true)
    {
        size_t bytesRead = 0;
        IoStatus status = recv_nonblocking(clientFd, buffer, sizeof(buffer), bytesRead);

        if (status == IoStatus::OK)
        {
            conn.inBuffer.append(buffer, bytesRead);
        }
        else if (status == IoStatus::WOULD_BLOCK)
        {
            break;
        }
        else
        {
            // Conexão fechada pelo cliente ou erro de leitura
            closeConnection(clientFd);
            return;
        }
    }

    processInputBuffer(conn);
    flushOutput(conn);
}

void ConnectionManager::processInputBuffer(Connection &conn)
{
    while (!conn.inBuffer.empty())
    {
        vector<string> tokens;
        size_t consumed = 0;
        Protocol::ParseStatus status = Protocol::parseCommand(conn.inBuffer, tokens, consumed);

        if (status == Protocol::ParseStatus::OK)
        {
            conn.inBuffer.erase(0, consumed);
            bool shouldClose = false;
            string response = commandHandler.execute(tokens, shouldClose);
            conn.outBuffer.append(response);
            if (shouldClose)
            {
                conn.shouldClose = true;
            }
        }
        else if (status == Protocol::ParseStatus::INCOMPLETE)
        {
            break;
        }
        else
        {
            conn.inBuffer.erase(0, consumed);
            conn.outBuffer.append(Protocol::formatError("syntax error in command"));
        }
    }
}

void ConnectionManager::flushOutput(Connection &conn)
{
    while (!conn.outBuffer.empty())
    {
        size_t bytesSent = 0;
        IoStatus status = send_nonblocking(conn.fd, conn.outBuffer.data(), conn.outBuffer.size(), bytesSent);

        if (status == IoStatus::OK)
        {
            conn.outBuffer.erase(0, bytesSent);
        }
        else if (status == IoStatus::WOULD_BLOCK)
        {
            poller.setClientWriteWatch(conn.fd, true);
            return;
        }
        else
        {
            closeConnection(conn.fd);
            return;
        }
    }

    if (conn.shouldClose)
    {
        closeConnection(conn.fd);
        return;
    }

    poller.setClientWriteWatch(conn.fd, false);
}

void ConnectionManager::handleClientWrite(int clientFd)
{
    auto it = connections.find(clientFd);
    if (it != connections.end())
    {
        flushOutput(it->second);
    }
}

void ConnectionManager::closeConnection(int clientFd)
{
    poller.removeClient(clientFd);
    close_socket(clientFd);
    connections.erase(clientFd);
}

void ConnectionManager::closeAll()
{
    for (auto &pair : connections)
    {
        poller.removeClient(pair.first);
        close_socket(pair.first);
    }
    connections.clear();
}
