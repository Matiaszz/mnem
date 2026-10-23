#include "headers/network/CommandHandler.h"
#include "headers/network/Protocol.h"
#include <algorithm>
#include <sstream>
#include <fstream>
#include <cctype>

using namespace std;

CommandHandler::CommandHandler(FlatTable &tbl)
    : table(tbl)
{
}

string CommandHandler::toUpper(string str)
{
    for (char &c : str)
    {
        c = static_cast<char>(toupper(static_cast<unsigned char>(c)));
    }
    return str;
}

string CommandHandler::execute(const vector<string> &tokens, bool &shouldClose)
{
    shouldClose = false;
    if (tokens.empty())
    {
        return "";
    }

    string cmd = toUpper(tokens[0]);

    if (cmd == "PING")
    {
        return handlePing(tokens);
    }
    if (cmd == "SET")
    {
        return handleSet(tokens);
    }
    if (cmd == "GET")
    {
        return handleGet(tokens);
    }
    if (cmd == "DEL")
    {
        return handleDel(tokens);
    }
    if (cmd == "EXISTS")
    {
        return handleExists(tokens);
    }
    if (cmd == "INCR")
    {
        return handleIncr(tokens);
    }
    if (cmd == "INCRBY")
    {
        return handleIncrBy(tokens);
    }
    if (cmd == "DECR")
    {
        return handleDecr(tokens);
    }
    if (cmd == "DECRBY")
    {
        return handleDecrBy(tokens);
    }
    if (cmd == "EXPIRE")
    {
        return handleExpire(tokens);
    }
    if (cmd == "PEXPIRE")
    {
        return handlePExpire(tokens);
    }
    if (cmd == "TTL")
    {
        return handleTtl(tokens);
    }
    if (cmd == "PTTL")
    {
        return handlePTtl(tokens);
    }
    if (cmd == "PERSIST")
    {
        return handlePersist(tokens);
    }
    if (cmd == "RENAME")
    {
        return handleRename(tokens);
    }
    if (cmd == "RENAMENX")
    {
        return handleRenameNx(tokens);
    }
    if (cmd == "PURGE")
    {
        return handlePurge(tokens);
    }
    if (cmd == "DUMP")
    {
        return handleDump(tokens);
    }
    if (cmd == "DBSIZE" || cmd == "COUNT")
    {
        return handleDbSize(tokens);
    }
    if (cmd == "INFO")
    {
        return handleInfo(tokens);
    }
    if (cmd == "QUIT" || cmd == "EXIT")
    {
        table.dump();
        shouldClose = true;
        return Protocol::formatSimpleString("OK");
    }
    if (cmd == "HELP")
    {
        return handleHelp(tokens);
    }

    return Protocol::formatError("unknown command '" + tokens[0] + "'");
}

size_t CommandHandler::replayAof()
{
    const string &aofPath = table.getAof().getFilePath();
    ifstream in(aofPath, ios::in | ios::binary);
    if (!in.is_open())
    {
        return 0;
    }

    string content((istreambuf_iterator<char>(in)), istreambuf_iterator<char>());
    in.close();

    if (content.empty())
    {
        return 0;
    }

    // Suspende a escrita no AOF durante a reexecução para não duplicar os comandos
    table.getAof().setEnabled(false);

    size_t replayedCount = 0;
    string_view remaining(content);

    while (!remaining.empty())
    {
        vector<string> tokens;
        size_t consumed = 0;
        Protocol::ParseStatus status = Protocol::parseCommand(remaining, tokens, consumed);

        if (status == Protocol::ParseStatus::OK)
        {
            remaining.remove_prefix(consumed);
            bool dummyClose = false;
            execute(tokens, dummyClose);
            ++replayedCount;
        }
        else if (status == Protocol::ParseStatus::ERROR)
        {
            if (consumed > 0)
            {
                remaining.remove_prefix(consumed);
            }
            else
            {
                break;
            }
        }
        else
        {
            // Caso a última linha não tenha quebra de linha ao final
            string tailLine(remaining);
            tailLine.push_back('\n');
            if (Protocol::parseCommand(tailLine, tokens, consumed) == Protocol::ParseStatus::OK)
            {
                bool dummyClose = false;
                execute(tokens, dummyClose);
                ++replayedCount;
            }
            break;
        }
    }

    table.getAof().setEnabled(true);
    table.clearAof();
    return replayedCount;
}

string CommandHandler::handlePing(const vector<string> &tokens)
{
    if (tokens.size() == 1)
    {
        return Protocol::formatSimpleString("PONG");
    }
    if (tokens.size() == 2)
    {
        return Protocol::formatBulkString(tokens[1]);
    }
    return Protocol::formatError("wrong number of arguments for 'ping' command");
}

string CommandHandler::handleSet(const vector<string> &tokens)
{
    // Sintaxe: SET key value [EX seconds] [PX milliseconds]
    if (tokens.size() < 3)
    {
        return Protocol::formatError("wrong number of arguments for 'set' command");
    }

    const string &key = tokens[1];
    const string &valStr = tokens[2];

    int64_t expireSec = 0;
    int64_t expireMs = 0;
    for (size_t i = 3; i < tokens.size(); ++i)
    {
        string opt = toUpper(tokens[i]);
        if (opt == "EX" && i + 1 < tokens.size())
        {
            try
            {
                int64_t sec = stoll(tokens[++i]);
                if (sec <= 0)
                {
                    return Protocol::formatError("invalid expire time in 'set' command");
                }
                expireSec = sec;
                expireMs = 0;
            }
            catch (...)
            {
                return Protocol::formatError("value is not an integer or out of range");
            }
        }
        else if (opt == "PX" && i + 1 < tokens.size())
        {
            try
            {
                int64_t ms = stoll(tokens[++i]);
                if (ms <= 0)
                {
                    return Protocol::formatError("invalid expire time in 'set' command");
                }
                expireMs = ms;
                expireSec = 0;
            }
            catch (...)
            {
                return Protocol::formatError("value is not an integer or out of range");
            }
        }
        else
        {
            return Protocol::formatError("syntax error in 'set' command");
        }
    }

    // Identifica o tipo correto do dado: JSON, Inteiro, Float ou String
    auto trimSpaces = [](const string &s) {
        size_t start = s.find_first_not_of(" \t\r\n");
        if (start == string::npos) return string();
        size_t end = s.find_last_not_of(" \t\r\n");
        return s.substr(start, end - start + 1);
    };

    string trimmed = trimSpaces(valStr);
    bool isJsonVal = false;
    if (trimmed.size() >= 2 &&
        ((trimmed.front() == '{' && trimmed.back() == '}') ||
         (trimmed.front() == '[' && trimmed.back() == ']')))
    {
        isJsonVal = true;
    }

    bool isInt = false;
    int64_t intVal = 0;
    if (!isJsonVal)
    {
        try
        {
            size_t idx = 0;
            intVal = stoll(valStr, &idx);
            if (idx == valStr.size())
            {
                isInt = true;
            }
        }
        catch (...)
        {
            isInt = false;
        }
    }

    bool isFloatVal = false;
    double doubleVal = 0.0;
    if (!isJsonVal && !isInt && (valStr.find('.') != string::npos || valStr.find('e') != string::npos || valStr.find('E') != string::npos))
    {
        try
        {
            size_t idx = 0;
            doubleVal = stod(valStr, &idx);
            if (idx == valStr.size())
            {
                isFloatVal = true;
            }
        }
        catch (...)
        {
            isFloatVal = false;
        }
    }

    Value valVariant;
    if (isJsonVal)
    {
        valVariant = Json{valStr};
    }
    else if (isInt)
    {
        valVariant = intVal;
    }
    else if (isFloatVal)
    {
        valVariant = doubleVal;
    }
    else
    {
        valVariant = string_view(valStr);
    }

    bool success = false;
    if (expireSec > 0)
    {
        success = table.setEx(key, valVariant, expireSec);
    }
    else if (expireMs > 0)
    {
        success = table.setPx(key, valVariant, expireMs);
    }
    else
    {
        success = table.set(key, valVariant);
    }

    if (success)
    {
        return Protocol::formatSimpleString("OK");
    }

    return Protocol::formatError("could not store key-value pair");
}

string CommandHandler::handleGet(const vector<string> &tokens)
{
    if (tokens.size() != 2)
    {
        return Protocol::formatError("wrong number of arguments for 'get' command");
    }

    auto optView = table.get(tokens[1]);
    if (!optView.has_value())
    {
        return Protocol::formatNull();
    }

    const ValueView &vv = *optView;
    if (vv.type == ValueType::INTEGER)
    {
        auto iv = vv.asInt();
        return Protocol::formatBulkString(to_string(iv ? *iv : 0));
    }
    if (vv.type == ValueType::FLOAT)
    {
        auto fv = vv.asFloat();
        string s = to_string(fv ? *fv : 0.0);
        if (s.find('.') != string::npos)
        {
            while (s.size() > 1 && s.back() == '0' && *(s.rbegin() + 1) != '.')
            {
                s.pop_back();
            }
        }
        return Protocol::formatBulkString(s);
    }

    return Protocol::formatBulkString(vv.asString());
}

string CommandHandler::handleDel(const vector<string> &tokens)
{
    if (tokens.size() < 2)
    {
        return Protocol::formatError("wrong number of arguments for 'del' command");
    }

    int64_t count = 0;
    for (size_t i = 1; i < tokens.size(); ++i)
    {
        if (table.del(tokens[i]))
        {
            ++count;
        }
    }
    return Protocol::formatInteger(count);
}

string CommandHandler::handleExists(const vector<string> &tokens)
{
    if (tokens.size() < 2)
    {
        return Protocol::formatError("wrong number of arguments for 'exists' command");
    }

    int64_t count = 0;
    for (size_t i = 1; i < tokens.size(); ++i)
    {
        if (table.exists(tokens[i]))
        {
            ++count;
        }
    }
    return Protocol::formatInteger(count);
}

string CommandHandler::handleIncr(const vector<string> &tokens)
{
    if (tokens.size() != 2)
    {
        return Protocol::formatError("wrong number of arguments for 'incr' command");
    }

    vector<string> incrByTokens = {tokens[0], tokens[1], "1"};
    return handleIncrBy(incrByTokens);
}

string CommandHandler::handleIncrBy(const vector<string> &tokens)
{
    if (tokens.size() != 3)
    {
        return Protocol::formatError("wrong number of arguments for 'incrby' command");
    }

    const string &key = tokens[1];
    int64_t amount = 0;
    try
    {
        amount = stoll(tokens[2]);
    }
    catch (...)
    {
        return Protocol::formatError("value is not an integer or out of range");
    }

    if (!table.exists(key))
    {
        if (table.set(key, amount))
        {
            return Protocol::formatInteger(amount);
        }
        return Protocol::formatError("failed to set value");
    }

    auto view = table.get(key);
    if (!view)
    {
        return Protocol::formatError("failed to read value");
    }

    // Se o valor estiver como STRING mas contiver um inteiro válido, normaliza para INTEGER
    if (view->type == ValueType::STRING)
    {
        try
        {
            int64_t current = stoll(string(view->asString()));
            int64_t updated = current + amount;
            if (table.set(key, updated))
            {
                return Protocol::formatInteger(updated);
            }
        }
        catch (...)
        {
            return Protocol::formatError("value is not an integer or out of range");
        }
    }

    if (amount >= 0)
    {
        if (table.increment(key, static_cast<uint64_t>(amount)))
        {
            auto nv = table.get(key);
            int64_t res = nv && nv->asInt() ? *nv->asInt() : 0;
            return Protocol::formatInteger(res);
        }
    }
    else
    {
        if (table.decrement(key, static_cast<uint64_t>(-amount)))
        {
            auto nv = table.get(key);
            int64_t res = nv && nv->asInt() ? *nv->asInt() : 0;
            return Protocol::formatInteger(res);
        }
    }

    return Protocol::formatError("value is not an integer or out of range");
}

string CommandHandler::handleDecr(const vector<string> &tokens)
{
    if (tokens.size() != 2)
    {
        return Protocol::formatError("wrong number of arguments for 'decr' command");
    }

    vector<string> decrByTokens = {tokens[0], tokens[1], "1"};
    return handleDecrBy(decrByTokens);
}

string CommandHandler::handleDecrBy(const vector<string> &tokens)
{
    if (tokens.size() != 3)
    {
        return Protocol::formatError("wrong number of arguments for 'decrby' command");
    }

    const string &key = tokens[1];
    int64_t amount = 0;
    try
    {
        amount = stoll(tokens[2]);
    }
    catch (...)
    {
        return Protocol::formatError("value is not an integer or out of range");
    }

    vector<string> incrByTokens = {"INCRBY", key, to_string(-amount)};
    return handleIncrBy(incrByTokens);
}

string CommandHandler::handleExpire(const vector<string> &tokens)
{
    if (tokens.size() != 3)
    {
        return Protocol::formatError("wrong number of arguments for 'expire' command");
    }

    int64_t seconds = 0;
    try
    {
        seconds = stoll(tokens[2]);
    }
    catch (...)
    {
        return Protocol::formatError("value is not an integer or out of range");
    }

    bool ok = table.expire(tokens[1], seconds);
    return Protocol::formatInteger(ok ? 1 : 0);
}

string CommandHandler::handlePExpire(const vector<string> &tokens)
{
    if (tokens.size() != 3)
    {
        return Protocol::formatError("wrong number of arguments for 'pexpire' command");
    }

    int64_t ms = 0;
    try
    {
        ms = stoll(tokens[2]);
    }
    catch (...)
    {
        return Protocol::formatError("value is not an integer or out of range");
    }

    bool ok = table.pexpire(tokens[1], ms);
    return Protocol::formatInteger(ok ? 1 : 0);
}

string CommandHandler::handleTtl(const vector<string> &tokens)
{
    if (tokens.size() != 2)
    {
        return Protocol::formatError("wrong number of arguments for 'ttl' command");
    }

    int64_t t = table.ttl(tokens[1]);
    return Protocol::formatInteger(t);
}

string CommandHandler::handlePTtl(const vector<string> &tokens)
{
    if (tokens.size() != 2)
    {
        return Protocol::formatError("wrong number of arguments for 'pttl' command");
    }

    int64_t t = table.pttl(tokens[1]);
    return Protocol::formatInteger(t);
}

string CommandHandler::handlePersist(const vector<string> &tokens)
{
    if (tokens.size() != 2)
    {
        return Protocol::formatError("wrong number of arguments for 'persist' command");
    }

    bool ok = table.persist(tokens[1]);
    return Protocol::formatInteger(ok ? 1 : 0);
}

string CommandHandler::handleRename(const vector<string> &tokens)
{
    if (tokens.size() != 3)
    {
        return Protocol::formatError("wrong number of arguments for 'rename' command");
    }

    bool ok = table.rename(tokens[1], tokens[2], false);
    if (ok)
    {
        return Protocol::formatSimpleString("OK");
    }
    return Protocol::formatError("no such key");
}

string CommandHandler::handleRenameNx(const vector<string> &tokens)
{
    if (tokens.size() != 3)
    {
        return Protocol::formatError("wrong number of arguments for 'renamenx' command");
    }

    bool ok = table.rename(tokens[1], tokens[2], true);
    return Protocol::formatInteger(ok ? 1 : 0);
}

string CommandHandler::handlePurge(const vector<string> &tokens)
{
    (void)tokens;
    size_t count = table.purgeExpired();
    return Protocol::formatInteger(static_cast<int64_t>(count));
}

string CommandHandler::handleDump(const vector<string> &tokens)
{
    if (tokens.size() != 1)
    {
        return Protocol::formatError("wrong number of arguments for 'dump' command");
    }

    if (table.dump())
    {
        return Protocol::formatSimpleString("OK");
    }
    return Protocol::formatError("failed to generate dump");
}

string CommandHandler::handleDbSize(const vector<string> &tokens)
{
    (void)tokens;
    return Protocol::formatInteger(static_cast<int64_t>(table.getActiveCount()));
}

string CommandHandler::handleInfo(const vector<string> &tokens)
{
    (void)tokens;
    const SlabAllocator &alloc = table.getAllocator();

    stringstream ss;
    ss << "# Storage\r\n";
    ss << "active_keys:" << table.getActiveCount() << "\r\n";
    ss << "table_capacity:" << table.getCapacity() << "\r\n";
    ss << "# Memory\r\n";
    ss << "slab_block_size:" << alloc.getBlockSize() << "\r\n";
    ss << "slab_total_blocks:" << alloc.getTotalBlocks() << "\r\n";
    ss << "slab_allocated_blocks:" << alloc.getAllocatedBlocks() << "\r\n";
    ss << "slab_free_blocks:" << alloc.getFreeBlocks() << "\r\n";
    ss << "slab_chunks:" << alloc.getChunkCount() << "\r\n";

    return Protocol::formatBulkString(ss.str());
}

string CommandHandler::handleHelp(const vector<string> &tokens)
{
    (void)tokens;
    string helpText =
        "Available commands:\n"
        "  PING [message]\n"
        "  SET key value [EX seconds] [PX ms]\n"
        "  GET key\n"
        "  DEL key [key ...]\n"
        "  EXISTS key [key ...]\n"
        "  INCR key\n"
        "  INCRBY key amount\n"
        "  DECR key\n"
        "  DECRBY key amount\n"
        "  EXPIRE key seconds\n"
        "  PEXPIRE key milliseconds\n"
        "  TTL key\n"
        "  PTTL key\n"
        "  PERSIST key\n"
        "  RENAME key newkey\n"
        "  RENAMENX key newkey\n"
        "  PURGE\n"
        "  DUMP\n"
        "  DBSIZE\n"
        "  INFO\n"
        "  QUIT / EXIT";

    return Protocol::formatBulkString(helpText);
}
