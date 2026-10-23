#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>
#include <unordered_map>
#include <iostream>
#include <cstring>
#include <cerrno>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#ifdef ERROR
#undef ERROR
#endif
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/epoll.h>
#include <fcntl.h>
#include <unistd.h>
#endif

namespace mnem::platform
{

    enum class IoStatus
    {
        OK,
        WOULD_BLOCK,
        CLOSED,
        ERROR_STATE
    };

    struct SocketEvent
    {
        int fd{-1};
        bool readable{false};
        bool writable{false};
        bool error{false};
    };

    inline bool init_network()
    {
#ifdef _WIN32
        WSADATA wsaData{};
        return WSAStartup(MAKEWORD(2, 2), &wsaData) == 0;
#else
        return true;
#endif
    }

    inline void cleanup_network()
    {
#ifdef _WIN32
        WSACleanup();
#endif
    }

    inline void close_socket(int fd)
    {
        if (fd < 0)
            return;
#ifdef _WIN32
        closesocket(static_cast<SOCKET>(fd));
#else
        ::close(fd);
#endif
    }

    inline void set_nonblocking(int fd)
    {
#ifdef _WIN32
        u_long mode = 1;
        ioctlsocket(static_cast<SOCKET>(fd), FIONBIO, &mode);
#else
        int flags = fcntl(fd, F_GETFL, 0);
        if (flags != -1)
        {
            fcntl(fd, F_SETFL, flags | O_NONBLOCK);
        }
#endif
    }

    inline int create_listen_socket(const std::string &host, int port, int backlog = 128)
    {
#ifdef _WIN32
        SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (s == INVALID_SOCKET)
        {
            std::cerr << "[Server] Erro ao criar socket: " << WSAGetLastError() << "\n";
            return -1;
        }
        int serverFd = static_cast<int>(s);
#else
        int serverFd = socket(AF_INET, SOCK_STREAM, 0);
        if (serverFd < 0)
        {
            std::cerr << "[Server] Erro ao criar socket: " << std::strerror(errno) << "\n";
            return -1;
        }
#endif

        int opt = 1;
#ifdef _WIN32
        setsockopt(static_cast<SOCKET>(serverFd), SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char *>(&opt), sizeof(opt));
#else
        setsockopt(serverFd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
#endif

        set_nonblocking(serverFd);

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(static_cast<uint16_t>(port));

        if (inet_pton(AF_INET, host.c_str(), &addr.sin_addr) <= 0)
        {
            std::cerr << "[Server] Endereco de host invalido: " << host << "\n";
            close_socket(serverFd);
            return -1;
        }

#ifdef _WIN32
        if (::bind(static_cast<SOCKET>(serverFd), reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) == SOCKET_ERROR)
        {
            std::cerr << "[Server] Erro no bind para " << host << ":" << port << " : " << WSAGetLastError() << "\n";
            close_socket(serverFd);
            return -1;
        }
        if (listen(static_cast<SOCKET>(serverFd), backlog) == SOCKET_ERROR)
        {
            std::cerr << "[Server] Erro no listen: " << WSAGetLastError() << "\n";
            close_socket(serverFd);
            return -1;
        }
#else
        if (::bind(serverFd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) < 0)
        {
            std::cerr << "[Server] Erro no bind para " << host << ":" << port << " : " << std::strerror(errno) << "\n";
            close_socket(serverFd);
            return -1;
        }
        if (listen(serverFd, backlog) < 0)
        {
            std::cerr << "[Server] Erro no listen: " << std::strerror(errno) << "\n";
            close_socket(serverFd);
            return -1;
        }
#endif

        return serverFd;
    }

    inline bool accept_client(int serverFd, int &clientFd)
    {
        sockaddr_in clientAddr{};
        socklen_t clientLen = sizeof(clientAddr);
#ifdef _WIN32
        SOCKET s = accept(static_cast<SOCKET>(serverFd), reinterpret_cast<sockaddr *>(&clientAddr), &clientLen);
        if (s == INVALID_SOCKET)
        {
            clientFd = -1;
            return false;
        }
        clientFd = static_cast<int>(s);
#else
        clientFd = accept(serverFd, reinterpret_cast<sockaddr *>(&clientAddr), &clientLen);
        if (clientFd < 0)
        {
            return false;
        }
#endif
        set_nonblocking(clientFd);
        return true;
    }

    inline IoStatus recv_nonblocking(int fd, char *buffer, std::size_t bufSize, std::size_t &bytesRead)
    {
        bytesRead = 0;
#ifdef _WIN32
        int n = recv(static_cast<SOCKET>(fd), buffer, static_cast<int>(bufSize), 0);
        if (n > 0)
        {
            bytesRead = static_cast<std::size_t>(n);
            return IoStatus::OK;
        }
        if (n == 0)
        {
            return IoStatus::CLOSED;
        }
        if (WSAGetLastError() == WSAEWOULDBLOCK)
        {
            return IoStatus::WOULD_BLOCK;
        }
        return IoStatus::ERROR_STATE;
#else
        ssize_t n = recv(fd, buffer, bufSize, 0);
        if (n > 0)
        {
            bytesRead = static_cast<std::size_t>(n);
            return IoStatus::OK;
        }
        if (n == 0)
        {
            return IoStatus::CLOSED;
        }
        if (errno == EAGAIN || errno == EWOULDBLOCK)
        {
            return IoStatus::WOULD_BLOCK;
        }
        return IoStatus::ERROR_STATE;
#endif
    }

    inline IoStatus send_nonblocking(int fd, const char *data, std::size_t len, std::size_t &bytesSent)
    {
        bytesSent = 0;
#ifdef _WIN32
        int n = send(static_cast<SOCKET>(fd), data, static_cast<int>(len), 0);
        if (n > 0)
        {
            bytesSent = static_cast<std::size_t>(n);
            return IoStatus::OK;
        }
        if (n == SOCKET_ERROR && WSAGetLastError() == WSAEWOULDBLOCK)
        {
            return IoStatus::WOULD_BLOCK;
        }
        return IoStatus::ERROR_STATE;
#else
        ssize_t n = send(fd, data, len, MSG_NOSIGNAL);
        if (n > 0)
        {
            bytesSent = static_cast<std::size_t>(n);
            return IoStatus::OK;
        }
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
        {
            return IoStatus::WOULD_BLOCK;
        }
        return IoStatus::ERROR_STATE;
#endif
    }

    // Multiplexador de eventos I/O multiplataforma (epoll no Linux, select no Windows)
    class SocketPoller
    {
    private:
        int serverFd{-1};
#ifdef _WIN32
        // Mapa fd -> se aguarda evento de escrita (writable)
        std::unordered_map<int, bool> watchedClients;
#else
        int epollFd{-1};
#endif

    public:
        SocketPoller() = default;
        ~SocketPoller() { close(); }

        bool init(int listenFd)
        {
            serverFd = listenFd;
#ifdef _WIN32
            watchedClients.clear();
            return true;
#else
            epollFd = epoll_create1(0);
            if (epollFd < 0)
            {
                std::cerr << "[Server] Erro ao criar epoll: " << std::strerror(errno) << "\n";
                return false;
            }
            epoll_event ev{};
            ev.events = EPOLLIN;
            ev.data.fd = serverFd;
            if (epoll_ctl(epollFd, EPOLL_CTL_ADD, serverFd, &ev) < 0)
            {
                std::cerr << "[Server] Erro ao registrar serverFd no epoll: " << std::strerror(errno) << "\n";
                ::close(epollFd);
                epollFd = -1;
                return false;
            }
            return true;
#endif
        }

        bool addClient(int clientFd)
        {
#ifdef _WIN32
            watchedClients[clientFd] = false;
            return true;
#else
            epoll_event ev{};
            ev.events = EPOLLIN;
            ev.data.fd = clientFd;
            return epoll_ctl(epollFd, EPOLL_CTL_ADD, clientFd, &ev) == 0;
#endif
        }

        void setClientWriteWatch(int clientFd, bool watchWrite)
        {
#ifdef _WIN32
            auto it = watchedClients.find(clientFd);
            if (it != watchedClients.end())
            {
                it->second = watchWrite;
            }
#else
            epoll_event ev{};
            ev.events = EPOLLIN | (watchWrite ? EPOLLOUT : 0);
            ev.data.fd = clientFd;
            epoll_ctl(epollFd, EPOLL_CTL_MOD, clientFd, &ev);
#endif
        }

        void removeClient(int clientFd)
        {
#ifdef _WIN32
            watchedClients.erase(clientFd);
#else
            if (epollFd != -1)
            {
                epoll_ctl(epollFd, EPOLL_CTL_DEL, clientFd, nullptr);
            }
#endif
        }

        int wait(std::vector<SocketEvent> &outEvents, int timeoutMs)
        {
            outEvents.clear();
#ifdef _WIN32
            fd_set readSet;
            fd_set writeSet;
            fd_set exceptSet;
            FD_ZERO(&readSet);
            FD_ZERO(&writeSet);
            FD_ZERO(&exceptSet);

            FD_SET(static_cast<SOCKET>(serverFd), &readSet);
            for (const auto &pair : watchedClients)
            {
                SOCKET s = static_cast<SOCKET>(pair.first);
                FD_SET(s, &readSet);
                FD_SET(s, &exceptSet);
                if (pair.second)
                {
                    FD_SET(s, &writeSet);
                }
            }

            timeval tv{};
            tv.tv_sec = timeoutMs / 1000;
            tv.tv_usec = (timeoutMs % 1000) * 1000;

            int n = select(0, &readSet, &writeSet, &exceptSet, &tv);
            if (n == SOCKET_ERROR)
            {
                return -1;
            }
            if (n == 0)
            {
                return 0;
            }

            if (FD_ISSET(static_cast<SOCKET>(serverFd), &readSet))
            {
                outEvents.push_back({serverFd, true, false, false});
            }

            for (const auto &pair : watchedClients)
            {
                int fd = pair.first;
                SOCKET s = static_cast<SOCKET>(fd);
                bool r = FD_ISSET(s, &readSet) != 0;
                bool w = FD_ISSET(s, &writeSet) != 0;
                bool e = FD_ISSET(s, &exceptSet) != 0;
                if (r || w || e)
                {
                    outEvents.push_back({fd, r, w, e});
                }
            }
            return static_cast<int>(outEvents.size());
#else
            constexpr int MAX_EVENTS = 64;
            epoll_event events[MAX_EVENTS];
            int n = epoll_wait(epollFd, events, MAX_EVENTS, timeoutMs);
            if (n < 0)
            {
                if (errno == EINTR)
                {
                    return 0;
                }
                return -1;
            }
            outEvents.reserve(static_cast<std::size_t>(n));
            for (int i = 0; i < n; ++i)
            {
                uint32_t flags = events[i].events;
                outEvents.push_back({
                    events[i].data.fd,
                    (flags & EPOLLIN) != 0,
                    (flags & EPOLLOUT) != 0,
                    (flags & (EPOLLERR | EPOLLHUP)) != 0,
                });
            }
            return n;
#endif
        }

        void close()
        {
#ifdef _WIN32
            watchedClients.clear();
#else
            if (epollFd != -1)
            {
                if (serverFd != -1)
                {
                    epoll_ctl(epollFd, EPOLL_CTL_DEL, serverFd, nullptr);
                }
                ::close(epollFd);
                epollFd = -1;
            }
#endif
            serverFd = -1;
        }
    };

} // namespace mnem::platform
