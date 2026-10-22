#include <iostream>
#include <string>
#include <vector>
#include <sstream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstring>
#include <cerrno>

using namespace std;

class StorageClient
{
public:
    StorageClient(const string &host = "127.0.0.1", int port = 6379)
        : host(host), port(port), sockFd(-1)
    {
    }

    ~StorageClient()
    {
        disconnect();
    }

    bool connectToServer()
    {
        sockFd = socket(AF_INET, SOCK_STREAM, 0);
        if (sockFd < 0)
        {
            cerr << "[Client] Erro ao criar socket: " << strerror(errno) << "\n";
            return false;
        }

        sockaddr_in serverAddr{};
        serverAddr.sin_family = AF_INET;
        serverAddr.sin_port = htons(static_cast<uint16_t>(port));

        if (inet_pton(AF_INET, host.c_str(), &serverAddr.sin_addr) <= 0)
        {
            cerr << "[Client] Endereço de host inválido: " << host << "\n";
            close(sockFd);
            sockFd = -1;
            return false;
        }

        if (connect(sockFd, reinterpret_cast<sockaddr *>(&serverAddr), sizeof(serverAddr)) < 0)
        {
            cerr << "[Client] Não foi possível conectar a " << host << ":" << port
                 << " (" << strerror(errno) << "). O servidor está em execução?\n";
            close(sockFd);
            sockFd = -1;
            return false;
        }

        return true;
    }

    void disconnect()
    {
        if (sockFd != -1)
        {
            close(sockFd);
            sockFd = -1;
        }
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

        ssize_t sent = send(sockFd, toSend.data(), toSend.size(), 0);
        if (sent < 0)
        {
            cerr << "[Client] Falha ao enviar comando: " << strerror(errno) << "\n";
            disconnect();
            return false;
        }

        return readResponse(responseFormatted);
    }

    const string &getHost() const { return host; }
    int getPort() const { return port; }

private:
    string host;
    int port;
    int sockFd;

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
        ssize_t n = recv(sockFd, &c, 1, 0);
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
            ssize_t n = recv(sockFd, &out[total], count - total, 0);
            if (n <= 0)
            {
                return false;
            }
            total += n;
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
         << "    ./mnem-cli GET usuario\n"
         << "    ./mnem-cli INCR visitas\n"
         << "    ./mnem-cli TTL visitas\n";
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
        if (arg == "--help" || arg == "-h")
        {
            if (idx + 1 < argc && argv[idx + 1][0] != '-')
            {
                host = argv[++idx];
            }
            else
            {
                printHelp();
                return 0;
            }
        }
        else if (arg == "-p" && idx + 1 < argc)
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
    cout << "  Digite comandos (ex: SET chave valor, GET chave, PING, HELP)\n";
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

        if (trimmed == "exit" || trimmed == "quit" || trimmed == "QUIT")
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
