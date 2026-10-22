#include "headers/network/Server.h"
#include "headers/network/Protocol.h"
#include <iostream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/epoll.h>
#include <fcntl.h>
#include <unistd.h>
#include <csignal>
#include <cstring>
#include <cerrno>

using namespace std;

static Server *g_activeServer = nullptr;

static void signalHandler(int sig)
{
    (void)sig;
    if (g_activeServer)
    {
        g_activeServer->stop();
    }
}

Server::Server(FlatTable &tbl, int port, const string &host)
    : table(tbl),
      commandHandler(tbl),
      port(port),
      host(host)
{
}

Server::~Server()
{
    stop();
}

void Server::setNonBlocking(int fd)
{
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags != -1)
    {
        fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    }
}

bool Server::initSocket()
{
    serverFd = socket(AF_INET, SOCK_STREAM, 0);
    if (serverFd < 0)
    {
        cerr << "[Server] Erro ao criar socket: " << strerror(errno) << "\n";
        return false;
    }

    int opt = 1;
    if (setsockopt(serverFd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0)
    {
        cerr << "[Server] Aviso ao definir SO_REUSEADDR: " << strerror(errno) << "\n";
    }

    setNonBlocking(serverFd);

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<uint16_t>(port));

    if (inet_pton(AF_INET, host.c_str(), &addr.sin_addr) <= 0)
    {
        cerr << "[Server] Endereço de host inválido: " << host << "\n";
        close(serverFd);
        serverFd = -1;
        return false;
    }

    if (::bind(serverFd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) < 0)
    {
        cerr << "[Server] Erro no bind para " << host << ":" << port << " : " << strerror(errno) << "\n";
        close(serverFd);
        serverFd = -1;
        return false;
    }

    if (listen(serverFd, 128) < 0)
    {
        cerr << "[Server] Erro no listen: " << strerror(errno) << "\n";
        close(serverFd);
        serverFd = -1;
        return false;
    }

    epollFd = epoll_create1(0);
    if (epollFd < 0)
    {
        cerr << "[Server] Erro ao criar epoll: " << strerror(errno) << "\n";
        close(serverFd);
        serverFd = -1;
        return false;
    }

    epoll_event ev{};
    ev.events = EPOLLIN;
    ev.data.fd = serverFd;
    if (epoll_ctl(epollFd, EPOLL_CTL_ADD, serverFd, &ev) < 0)
    {
        cerr << "[Server] Erro ao registrar serverFd no epoll: " << strerror(errno) << "\n";
        close(epollFd);
        close(serverFd);
        serverFd = -1;
        epollFd = -1;
        return false;
    }

    return true;
}

void Server::acceptConnection()
{
    while (true)
    {
        sockaddr_in clientAddr{};
        socklen_t clientLen = sizeof(clientAddr);
        int clientFd = accept(serverFd, reinterpret_cast<sockaddr *>(&clientAddr), &clientLen);

        if (clientFd < 0)
        {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
            {
                break;
            }
            break;
        }

        setNonBlocking(clientFd);

        epoll_event ev{};
        ev.events = EPOLLIN;
        ev.data.fd = clientFd;
        if (epoll_ctl(epollFd, EPOLL_CTL_ADD, clientFd, &ev) < 0)
        {
            close(clientFd);
            continue;
        }

        char ipStr[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &clientAddr.sin_addr, ipStr, sizeof(ipStr));

        Connection conn;
        conn.fd = clientFd;
        connections[clientFd] = std::move(conn);
    }
}

void Server::flushOutput(Connection &conn)
{
    while (!conn.outBuffer.empty())
    {
        ssize_t sent = send(conn.fd, conn.outBuffer.data(), conn.outBuffer.size(), MSG_NOSIGNAL);
        if (sent > 0)
        {
            conn.outBuffer.erase(0, sent);
        }
        else
        {
            if (sent < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
            {
                // Buffer cheio, adiciona EPOLLOUT no epoll
                epoll_event ev{};
                ev.events = EPOLLIN | EPOLLOUT;
                ev.data.fd = conn.fd;
                epoll_ctl(epollFd, EPOLL_CTL_MOD, conn.fd, &ev);
                return;
            }
            // Erro fatal de envio
            closeConnection(conn.fd);
            return;
        }
    }

    // Todo o buffer de saída foi descarregado
    if (conn.shouldClose)
    {
        closeConnection(conn.fd);
        return;
    }

    epoll_event ev{};
    ev.events = EPOLLIN;
    ev.data.fd = conn.fd;
    epoll_ctl(epollFd, EPOLL_CTL_MOD, conn.fd, &ev);
}

void Server::handleClientRead(int clientFd)
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
        ssize_t bytesRead = recv(clientFd, buffer, sizeof(buffer), 0);
        if (bytesRead > 0)
        {
            conn.inBuffer.append(buffer, bytesRead);
        }
        else if (bytesRead == 0)
        {
            // Cliente encerrou conexão
            closeConnection(clientFd);
            return;
        }
        else
        {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
            {
                break;
            }
            closeConnection(clientFd);
            return;
        }
    }

    // Processa comandos contidos no buffer de entrada
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
            // Espera mais dados chegarem
            break;
        }
        else
        {
            // Erro de análise sintática
            conn.inBuffer.erase(0, consumed);
            conn.outBuffer.append(Protocol::formatError("syntax error in command"));
        }
    }

    flushOutput(conn);
}

void Server::handleClientWrite(int clientFd)
{
    auto it = connections.find(clientFd);
    if (it != connections.end())
    {
        flushOutput(it->second);
    }
}

void Server::closeConnection(int clientFd)
{
    epoll_ctl(epollFd, EPOLL_CTL_DEL, clientFd, nullptr);
    close(clientFd);
    connections.erase(clientFd);
}

bool Server::start()
{
    if (running.load())
    {
        return true;
    }

    signal(SIGPIPE, SIG_IGN);
    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);
    g_activeServer = this;

    if (!initSocket())
    {
        return false;
    }

    running.store(true);
    cout << "=========================================================\n";
    cout << "  MNEM In-Memory Storage Server iniciado com sucesso!\n";
    cout << "  Escutando em: " << host << ":" << port << "\n";
    cout << "  Aguardando conexoes... (Pressione Ctrl+C para encerrar)\n";
    cout << "=========================================================\n";

    constexpr int MAX_EVENTS = 64;
    epoll_event events[MAX_EVENTS];

    while (running.load())
    {
        // Timeout de 250ms permite executar periodicamente a limpeza de TTL
        int nEvents = epoll_wait(epollFd, events, MAX_EVENTS, 250);

        if (nEvents < 0)
        {
            if (errno == EINTR)
            {
                continue;
            }
            break;
        }

        for (int i = 0; i < nEvents; ++i)
        {
            int fd = events[i].data.fd;
            uint32_t evFlags = events[i].events;

            if (fd == serverFd)
            {
                acceptConnection();
            }
            else
            {
                if (evFlags & (EPOLLERR | EPOLLHUP))
                {
                    closeConnection(fd);
                    continue;
                }
                if (evFlags & EPOLLIN)
                {
                    handleClientRead(fd);
                }
                if ((evFlags & EPOLLOUT) && connections.count(fd))
                {
                    handleClientWrite(fd);
                }
            }
        }

        // Limpeza passiva/ativa periódica de chaves expiradas
        table.purgeExpired();
    }

    cout << "\n[Server] Encerrando servidor...\n";
    stop();
    cout << "[Server] Servidor encerrado.\n";
    return true;
}

void Server::stop()
{
    if (!running.exchange(false))
    {
        return;
    }

    for (auto &pair : connections)
    {
        epoll_ctl(epollFd, EPOLL_CTL_DEL, pair.first, nullptr);
        close(pair.first);
    }
    connections.clear();

    if (serverFd != -1)
    {
        epoll_ctl(epollFd, EPOLL_CTL_DEL, serverFd, nullptr);
        close(serverFd);
        serverFd = -1;
    }

    if (epollFd != -1)
    {
        close(epollFd);
        epollFd = -1;
    }

    g_activeServer = nullptr;
}
