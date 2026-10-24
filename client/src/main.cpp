#include <iostream>
#include <string>
#include <vector>
#include <sstream>
#include <cstring>
#include <cerrno>
#include <cstdint>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#endif

using namespace std;

class StorageClient
{
public:
    StorageClient(const string &host = "127.0.0.1", int port = 6379)
        : host(host), port(port), sockFd(-1)
    {
#ifdef _WIN32
        WSADATA wsaData{};
        WSAStartup(MAKEWORD(2, 2), &wsaData);
#endif
    }

    ~StorageClient()
    {
        disconnect();
#ifdef _WIN32
        WSACleanup();
#endif
    }

    bool connectToServer()
    {
#ifdef _WIN32
        SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (s == INVALID_SOCKET)
        {
            cerr << "[Client] Erro ao criar socket: " << WSAGetLastError() << "\n";
            return false;
        }
        sockFd = static_cast<int>(s);
#else
        sockFd = socket(AF_INET, SOCK_STREAM, 0);
        if (sockFd < 0)
        {
            cerr << "[Client] Erro ao criar socket: " << strerror(errno) << "\n";
            return false;
        }
#endif

        sockaddr_in serverAddr{};
        serverAddr.sin_family = AF_INET;
        serverAddr.sin_port = htons(static_cast<uint16_t>(port));

        if (inet_pton(AF_INET, host.c_str(), &serverAddr.sin_addr) <= 0)
        {
            cerr << "[Client] Endereço de host inválido: " << host << "\n";
            closeSocket();
            return false;
        }

#ifdef _WIN32
        if (connect(static_cast<SOCKET>(sockFd), reinterpret_cast<sockaddr *>(&serverAddr), sizeof(serverAddr)) == SOCKET_ERROR)
        {
            cerr << "[Client] Não foi possível conectar a " << host << ":" << port
                 << " (erro " << WSAGetLastError() << "). O servidor está em execução?\n";
            closeSocket();
            return false;
        }
#else
        if (connect(sockFd, reinterpret_cast<sockaddr *>(&serverAddr), sizeof(serverAddr)) < 0)
        {
            cerr << "[Client] Não foi possível conectar a " << host << ":" << port
                 << " (" << strerror(errno) << "). O servidor está em execução?\n";
            closeSocket();
            return false;
        }
#endif

        return true;
    }

    void disconnect()
    {
        closeSocket();
    }

    bool isConnected() const
    {
        return sockFd != -1;
    }

    bool sendCommand(const string &rawCmd, string &responseFormatted)
    {
        responseFormatted.clear();
        if (sockFd == -1)
        {
            return false;
        }

        string toSend = rawCmd;
        if (toSend.empty() || toSend.back() != '\n')
        {
            toSend += "\r\n";
        }

#ifdef _WIN32
        int sent = send(static_cast<SOCKET>(sockFd), toSend.data(), static_cast<int>(toSend.size()), 0);
        if (sent == SOCKET_ERROR)
        {
            cerr << "[Client] Falha ao enviar comando: " << WSAGetLastError() << "\n";
            disconnect();
            return false;
        }
#else
        ssize_t sent = send(sockFd, toSend.data(), toSend.size(), 0);
        if (sent < 0)
        {
            cerr << "[Client] Falha ao enviar comando: " << strerror(errno) << "\n";
            disconnect();
            return false;
        }
#endif

        return readResponse(responseFormatted);
    }

    const string &getHost() const { return host; }
    int getPort() const { return port; }

private:
    string host;
    int port;
    int sockFd;

    void closeSocket()
    {
        if (sockFd != -1)
        {
#ifdef _WIN32
            closesocket(static_cast<SOCKET>(sockFd));
#else
            close(sockFd);
#endif
            sockFd = -1;
        }
    }

    static bool isNumeric(const string &s)
    {
        if (s.empty()) return false;
        size_t i = 0;
        if (s[0] == '+' || s[0] == '-') i++;
        if (i >= s.size()) return false;

        bool hasDot = false;
        bool hasDigits = false;
        for (; i < s.size(); ++i)
        {
            if (s[i] == '.')
            {
                if (hasDot) return false;
                hasDot = true;
            }
            else if (isdigit(static_cast<unsigned char>(s[i])))
            {
                hasDigits = true;
            }
            else if (i > 0 && (s[i] == 'e' || s[i] == 'E') && hasDigits && i + 1 < s.size())
            {
                i++;
                if (s[i] == '+' || s[i] == '-') i++;
                if (i >= s.size() || !isdigit(static_cast<unsigned char>(s[i]))) return false;
            }
            else
            {
                return false;
            }
        }
        return hasDigits;
    }

    static bool isJson(const string &s)
    {
        size_t start = s.find_first_not_of(" \t\r\n");
        if (start == string::npos) return false;
        size_t end = s.find_last_not_of(" \t\r\n");

        char first = s[start];
        char last = s[end];

        if ((first == '{' && last == '}') || (first == '[' && last == ']'))
        {
            int braceCount = 0;
            int bracketCount = 0;
            bool inQuotes = false;
            bool escape = false;

            for (size_t i = start; i <= end; ++i)
            {
                char c = s[i];
                if (escape)
                {
                    escape = false;
                    continue;
                }
                if (c == '\\' && inQuotes)
                {
                    escape = true;
                    continue;
                }
                if (c == '"')
                {
                    inQuotes = !inQuotes;
                    continue;
                }
                if (!inQuotes)
                {
                    if (c == '{') braceCount++;
                    else if (c == '}') braceCount--;
                    else if (c == '[') bracketCount++;
                    else if (c == ']') bracketCount--;

                    if (braceCount < 0 || bracketCount < 0) return false;
                }
            }

            return !inQuotes && braceCount == 0 && bracketCount == 0;
        }
        return false;
    }

    static bool isBoolean(const string &s)
    {
        return (s == "true" || s == "false" || s == "TRUE" || s == "FALSE");
    }

    static string formatTypedValue(const string &data)
    {
        if (data.find('\n') != string::npos)
        {
            string cleaned = data;
            while (!cleaned.empty() && (cleaned.back() == '\r' || cleaned.back() == '\n'))
            {
                cleaned.pop_back();
            }
            return cleaned;
        }
        if (isNumeric(data))
        {
            return data;
        }
        if (isJson(data))
        {
            return data;
        }
        if (isBoolean(data))
        {
            return data;
        }
        if (data == "null" || data == "NULL")
        {
            return data;
        }
        return "\"" + data + "\"";
    }

    bool readByte(char &c)
    {
#ifdef _WIN32
        int n = recv(static_cast<SOCKET>(sockFd), &c, 1, 0);
#else
        ssize_t n = recv(sockFd, &c, 1, 0);
#endif
        if (n <= 0)
        {
            return false;
        }
        return true;
    }

    bool readLine(string &line)
    {
        line.clear();
        char c;
        while (readByte(c))
        {
            if (c == '\r')
            {
                char next;
                if (readByte(next) && next == '\n')
                {
                    break;
                }
                line += c;
                line += next;
            }
            else if (c == '\n')
            {
                break;
            }
            else
            {
                line += c;
            }
        }
        return !line.empty() || sockFd != -1;
    }

    bool readExact(size_t count, string &out)
    {
        out.clear();
        out.resize(count);
        size_t total = 0;
        while (total < count)
        {
#ifdef _WIN32
            int n = recv(static_cast<SOCKET>(sockFd), &out[total], static_cast<int>(count - total), 0);
#else
            ssize_t n = recv(sockFd, &out[total], count - total, 0);
#endif
            if (n <= 0)
            {
                return false;
            }
            total += static_cast<size_t>(n);
        }
        return true;
    }

    bool readResponse(string &formatted)
    {
        char prefix;
        if (!readByte(prefix))
        {
            formatted = "(erro: conexão perdida com o servidor)";
            disconnect();
            return false;
        }

        switch (prefix)
        {
        case '+': // Simple String (+OK, +PONG)
        {
            string line;
            if (!readLine(line))
                return false;
            formatted = line;
            return true;
        }
        case '-': // Error (-ERR ...)
        {
            string line;
            if (!readLine(line))
                return false;
            formatted = "(error) " + line;
            return true;
        }
        case ':': // Integer (:123)
        {
            string line;
            if (!readLine(line))
                return false;
            formatted = "(integer) " + line;
            return true;
        }
        case '$': // Bulk String ($len\r\n...)
        {
            string lenStr;
            if (!readLine(lenStr))
                return false;
            int64_t len = 0;
            try
            {
                len = stoll(lenStr);
            }
            catch (...)
            {
                formatted = "(erro de protocolo)";
                return false;
            }

            if (len == -1)
            {
                formatted = "(nil)";
                return true;
            }

            string data;
            if (!readExact(static_cast<size_t>(len), data))
            {
                return false;
            }

            // Consome o \r\n final
            char cr, lf;
            readByte(cr);
            if (cr == '\r')
            {
                readByte(lf);
            }

            formatted = formatTypedValue(data);
            return true;
        }
        case '*': // Array (*count\r\n...)
        {
            string countStr;
            if (!readLine(countStr))
                return false;
            int64_t count = 0;
            try
            {
                count = stoll(countStr);
            }
            catch (...)
            {
                return false;
            }

            if (count == -1)
            {
                formatted = "(nil)";
                return true;
            }

            stringstream ss;
            for (int64_t i = 1; i <= count; ++i)
            {
                string item;
                if (!readResponse(item))
                    return false;
                ss << i << ") " << item;
                if (i < count)
                    ss << "\n";
            }
            formatted = ss.str();
            return true;
        }
        default:
        {
            // Resposta crua em linha
            string rest;
            readLine(rest);
            formatted = prefix + rest;
            return true;
        }
        }
    }
};

static void printHelp()
{
    cout << "Uso:\n"
         << "  Modo interativo:\n"
         << "    ./mnem-cli [host] [port]\n"
         << "  Modo comando único:\n"
         << "    ./mnem-cli [host] [port] <COMANDO> [ARGUMENTOS...]\n"
         << "    ./mnem-cli -p <porta> <COMANDO> [ARGUMENTOS...]\n"
         << "  Exemplos:\n"
         << "    ./mnem-cli PING\n"
         << "    ./mnem-cli SET usuario Allan\n"
         << "    ./mnem-cli SET sessao token123 EX 60\n"
         << "    ./mnem-cli SET cache dados PX 5000 NX\n"
         << "    ./mnem-cli GET usuario\n"
         << "    ./mnem-cli EXPIRE usuario 120\n"
         << "    ./mnem-cli INCR visitas 5\n"
         << "    ./mnem-cli TTL sessao\n"
         << "    ./mnem-cli HELP\n";
}

int main(int argc, char *argv[])
{
    string host = "127.0.0.1";
    int port = 6379;
    vector<string> cmdArgs;

    int idx = 1;
    while (idx < argc)
    {
        string arg = argv[idx];
        if (cmdArgs.empty() && (arg == "--help" || arg == "-h"))
        {
            if (arg == "-h" && idx + 1 < argc && argv[idx + 1][0] != '-')
            {
                host = argv[++idx];
            }
            else
            {
                printHelp();
                return 0;
            }
        }
        else if (cmdArgs.empty() && arg == "-p" && idx + 1 < argc)
        {
            try
            {
                port = stoi(argv[++idx]);
            }
            catch (...)
            {
                cerr << "Porta inválida: " << argv[idx] << "\n";
                return 1;
            }
        }
        else
        {
            // Verifica se é host ou porta posicional antes dos comandos
            if (cmdArgs.empty() && arg.find('.') != string::npos)
            {
                host = arg;
            }
            else if (cmdArgs.empty() && isdigit(static_cast<unsigned char>(arg[0])) && arg.find_first_not_of("0123456789") == string::npos)
            {
                port = stoi(arg);
            }
            else
            {
                cmdArgs.push_back(arg);
            }
        }
        ++idx;
    }

    StorageClient client(host, port);
    if (!client.connectToServer())
    {
        return 1;
    }

    // Modo 1: Execução de comando único via linha de comando
    if (!cmdArgs.empty())
    {
        // Envia via protocolo estruturado em array para total integridade de tipos e aspas (ex: JSON)
        string arrayCmd = "*" + to_string(cmdArgs.size()) + "\r\n";
        for (const auto &arg : cmdArgs)
        {
            arrayCmd += "$" + to_string(arg.size()) + "\r\n" + arg + "\r\n";
        }

        string response;
        if (client.sendCommand(arrayCmd, response))
        {
            cout << response << "\n";
            return 0;
        }
        return 1;
    }

    // Modo 2: Shell interativo (REPL)
    cout << "=========================================================\n";
    cout << "  MNEM CLI Client\n";
    cout << "  Conectado em " << client.getHost() << ":" << client.getPort() << "\n";
    cout << "  Digite comandos (ex: SET chave valor [EX seg], GET chave, PING, HELP)\n";
    cout << "  Digite 'quit' ou 'exit' para sair.\n";
    cout << "=========================================================\n";

    string line;
    while (client.isConnected())
    {
        cout << client.getHost() << ":" << client.getPort() << "> ";
        cout.flush();

        if (!getline(cin, line))
        {
            break; // EOF (Ctrl+D)
        }

        // Remove espaços em branco nas bordas
        size_t first = line.find_first_not_of(" \t\r\n");
        if (first == string::npos)
        {
            continue;
        }
        size_t last = line.find_last_not_of(" \t\r\n");
        string trimmed = line.substr(first, last - first + 1);

        if (trimmed == "exit" || trimmed == "quit" || trimmed == "QUIT" || trimmed == "EXIT")
        {
            string resp;
            client.sendCommand("QUIT", resp);
            if (!resp.empty())
            {
                cout << resp << "\n";
            }
            cout << "Conexão encerrada.\n";
            break;
        }

        string response;
        if (!client.sendCommand(trimmed, response))
        {
            cout << "Não foi possível obter resposta do servidor.\n";
            break;
        }

        cout << response << "\n";
    }

    return 0;
}
