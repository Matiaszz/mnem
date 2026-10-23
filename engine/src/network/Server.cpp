#include "headers/network/Server.h"
#include <iostream>
#include <vector>
#include <csignal>

using namespace std;
using namespace mnem::platform;

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
      poller(),
      connectionManager(commandHandler, poller),
      port(port),
      host(host)
{
}

Server::~Server()
{
    stop();
}

void Server::setupSignals()
{
#ifdef SIGPIPE
    signal(SIGPIPE, SIG_IGN);
#endif
    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);
    g_activeServer = this;
}

bool Server::start()
{
    if (running.load())
    {
        return true;
    }

    setupSignals();

    // 1. Carrega o Dump salvo em disco (se existir)
    if (table.loadDump())
    {
        cout << "[Server] Dump carregado com sucesso (" << table.getActiveCount() << " chaves restauradas).\n";
    }

    // 2. Executa os comandos pendentes no AOF (caso houver)
    size_t replayed = commandHandler.replayAof();
    if (replayed > 0)
    {
        cout << "[Server] AOF processado (" << replayed << " comandos reexecutados, "
             << table.getActiveCount() << " chaves ativas).\n";
    }

    // 3. Limpa o AOF após carregar o estado inicial
    table.clearAof();

    if (!init_network())
    {
        cerr << "[Server] Erro ao inicializar subsistema de rede.\n";
        return false;
    }

    serverFd = create_listen_socket(host, port);
    if (serverFd < 0)
    {
        cleanup_network();
        return false;
    }

    if (!poller.init(serverFd))
    {
        close_socket(serverFd);
        serverFd = -1;
        cleanup_network();
        return false;
    }

    running.store(true);
    cout << "=========================================================\n";
    cout << "  MNEM In-Memory Storage Server iniciado com sucesso!\n";
    cout << "  Escutando em: " << host << ":" << port << "\n";
    cout << "  Chaves ativas em memoria: " << table.getActiveCount() << "\n";
    cout << "  Aguardando conexoes... (Pressione Ctrl+C para encerrar)\n";
    cout << "=========================================================\n";

    runEventLoop();

    cout << "\n[Server] Encerrando servidor...\n";
    stop();
    cout << "[Server] Servidor encerrado.\n";
    return true;
}

void Server::runEventLoop()
{
    vector<SocketEvent> events;

    while (running.load())
    {
        // Timeout de 250ms permite executar periodicamente a limpeza ativa de TTL
        int nEvents = poller.wait(events, 250);
        if (nEvents < 0)
        {
            break;
        }

        for (const auto &ev : events)
        {
            if (ev.fd == serverFd)
            {
                connectionManager.acceptNewConnections(serverFd);
            }
            else
            {
                if (ev.error)
                {
                    connectionManager.closeConnection(ev.fd);
                    continue;
                }
                if (ev.readable)
                {
                    connectionManager.handleClientRead(ev.fd);
                }
                if (ev.writable && connectionManager.hasConnection(ev.fd))
                {
                    connectionManager.handleClientWrite(ev.fd);
                }
            }
        }

        table.purgeExpired();
    }
}

void Server::stop()
{
    if (!running.exchange(false))
    {
        return;
    }

    cout << "\n[Server] Salvando dump de memoria e limpando AOF...\n";
    if (table.dump())
    {
        cout << "[Server] Dump salvo com sucesso.\n";
    }

    connectionManager.closeAll();
    poller.close();

    if (serverFd != -1)
    {
        close_socket(serverFd);
        serverFd = -1;
    }

    cleanup_network();
    g_activeServer = nullptr;
}
