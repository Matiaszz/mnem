#include "headers/network/CommandHandler.h"
#include "headers/network/Protocol.h"
#include <algorithm>
#include <sstream>
#include <fstream>
#include <cctype>

using namespace std;

namespace
{
    string stripOptionBrackets(string s)
    {
        while (!s.empty() && s.front() == '[')
        {
            s.erase(s.begin());
        }
        while (!s.empty() && s.back() == ']')
        {
            s.pop_back();
        }
        return s;
    }

    bool parseStrictInt64(const string &str, int64_t &outVal)
    {
        string cleaned = stripOptionBrackets(str);
        if (cleaned.empty())
        {
            return false;
        }
        try
        {
            size_t idx = 0;
            outVal = stoll(cleaned, &idx);
            return idx == cleaned.size();
        }
        catch (...)
        {
            return false;
        }
    }

    bool checkExpireCondition(int64_t currentPttl, int64_t newRemainingMs, const string &cond)
    {
        if (cond.empty())
        {
            return true;
        }
        if (cond == "NX")
        {
            return currentPttl == -1;
        }
        if (cond == "XX")
        {
            return currentPttl >= 0;
        }
        if (cond == "GT")
        {
            // Em Redis, uma chave sem expiração (persistente) tem TTL infinito, portanto GT falha se currentPttl == -1
            return currentPttl >= 0 && newRemainingMs > currentPttl;
        }
        if (cond == "LT")
        {
            // Se não possui expiração (-1), o novo TTL finito é menor que infinito
            return currentPttl == -1 || (currentPttl >= 0 && newRemainingMs < currentPttl);
        }
        return false;
    }

    bool isValidExpireCondition(const string &cond)
    {
        return cond == "NX" || cond == "XX" || cond == "GT" || cond == "LT";
    }
}

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
    if (cmd == "SETEX")
    {
        return handleSetEx(tokens);
    }
    if (cmd == "PSETEX" || cmd == "SETPX")
    {
        return handlePSetEx(tokens);
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
    if (cmd == "EXPIREAT")
    {
        return handleExpireAt(tokens);
    }
    if (cmd == "PEXPIREAT")
    {
        return handlePExpireAt(tokens);
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
    string joined = tokens[1];
    for (size_t i = 2; i < tokens.size(); ++i)
    {
        joined += " " + tokens[i];
    }
    return Protocol::formatBulkString(joined);
}

string CommandHandler::handleSet(const vector<string> &tokens)
{
    // Sintaxe: SET key value [EX seconds | PX milliseconds | EXAT timestamp | PXAT ms-timestamp | KEEPTTL] [NX | XX] [GET]
    if (tokens.size() < 3)
    {
        return Protocol::formatError("wrong number of arguments for 'set' command");
    }

    const string &key = tokens[1];

    auto isSetOptionKeyword = [&](const string &rawTok) -> bool {
        string t = toUpper(stripOptionBrackets(rawTok));
        size_t eqPos = t.find('=');
        if (eqPos != string::npos)
        {
            t = t.substr(0, eqPos);
        }
        return (t == "EX" || t == "PX" || t == "EXPIRE" || t == "PEXPIRE" ||
                t == "TTL" || t == "PTTL" || t == "EXAT" || t == "PXAT" ||
                t == "EXPIREAT" || t == "PEXPIREAT" || t == "NX" || t == "XX" ||
                t == "KEEPTTL" || t == "GET");
    };

    // Identifica onde começam os parâmetros opcionais (suporta valores com espaços caso seguidos por opções válidas)
    size_t optStart = 3;
    if (tokens.size() > 3 && !isSetOptionKeyword(tokens[3]))
    {
        size_t foundOpt = tokens.size();
        for (size_t i = 4; i < tokens.size(); ++i)
        {
            if (isSetOptionKeyword(tokens[i]))
            {
                foundOpt = i;
                break;
            }
        }
        if (foundOpt < tokens.size())
        {
            optStart = foundOpt;
        }
    }

    string valStr = tokens[2];
    for (size_t i = 3; i < optStart; ++i)
    {
        valStr += " " + tokens[i];
    }

    int64_t expireSec = 0;
    int64_t expireMs = 0;
    uint64_t expireAtAbsMs = 0;
    bool onlyIfNotExists = false;
    bool onlyIfExists = false;
    bool keepTtl = false;
    bool returnOld = false;

    // Atalho posicional: SET key value <seconds> (ex: SET chave valor 10)
    if (optStart == 3 && tokens.size() == 4 && !isSetOptionKeyword(tokens[3]))
    {
        int64_t posSec = 0;
        if (parseStrictInt64(tokens[3], posSec))
        {
            if (posSec <= 0)
            {
                return Protocol::formatError("invalid expire time in 'set' command");
            }
            expireSec = posSec;
            optStart = 4;
        }
    }

    for (size_t i = optStart; i < tokens.size(); ++i)
    {
        string rawOpt = stripOptionBrackets(tokens[i]);
        string opt = toUpper(rawOpt);
        string inlineArg;
        size_t eqPos = opt.find('=');
        if (eqPos != string::npos)
        {
            inlineArg = rawOpt.substr(eqPos + 1);
            opt = opt.substr(0, eqPos);
        }

        auto nextArg = [&](string &outArg) -> bool {
            if (!inlineArg.empty())
            {
                outArg = inlineArg;
                return true;
            }
            if (i + 1 < tokens.size())
            {
                outArg = stripOptionBrackets(tokens[++i]);
                return true;
            }
            return false;
        };

        if (opt == "EX" || opt == "EXPIRE" || opt == "TTL")
        {
            string argStr;
            if (!nextArg(argStr))
            {
                return Protocol::formatError("syntax error in 'set' command");
            }
            int64_t sec = 0;
            if (!parseStrictInt64(argStr, sec))
            {
                return Protocol::formatError("value is not an integer or out of range");
            }
            if (sec <= 0)
            {
                return Protocol::formatError("invalid expire time in 'set' command");
            }
            expireSec = sec;
            expireMs = 0;
            expireAtAbsMs = 0;
            keepTtl = false;
        }
        else if (opt == "PX" || opt == "PEXPIRE" || opt == "PTTL")
        {
            string argStr;
            if (!nextArg(argStr))
            {
                return Protocol::formatError("syntax error in 'set' command");
            }
            int64_t ms = 0;
            if (!parseStrictInt64(argStr, ms))
            {
                return Protocol::formatError("value is not an integer or out of range");
            }
            if (ms <= 0)
            {
                return Protocol::formatError("invalid expire time in 'set' command");
            }
            expireMs = ms;
            expireSec = 0;
            expireAtAbsMs = 0;
            keepTtl = false;
        }
        else if (opt == "EXAT" || opt == "EXPIREAT")
        {
            string argStr;
            if (!nextArg(argStr))
            {
                return Protocol::formatError("syntax error in 'set' command");
            }
            int64_t unixSec = 0;
            if (!parseStrictInt64(argStr, unixSec))
            {
                return Protocol::formatError("value is not an integer or out of range");
            }
            if (unixSec <= 0)
            {
                return Protocol::formatError("invalid expire time in 'set' command");
            }
            expireAtAbsMs = static_cast<uint64_t>(unixSec) * 1000;
            expireSec = 0;
            expireMs = 0;
            keepTtl = false;
        }
        else if (opt == "PXAT" || opt == "PEXPIREAT")
        {
            string argStr;
            if (!nextArg(argStr))
            {
                return Protocol::formatError("syntax error in 'set' command");
            }
            int64_t unixMs = 0;
            if (!parseStrictInt64(argStr, unixMs))
            {
                return Protocol::formatError("value is not an integer or out of range");
            }
            if (unixMs <= 0)
            {
                return Protocol::formatError("invalid expire time in 'set' command");
            }
            expireAtAbsMs = static_cast<uint64_t>(unixMs);
            expireSec = 0;
            expireMs = 0;
            keepTtl = false;
        }
        else if (opt == "NX")
        {
            onlyIfNotExists = true;
        }
        else if (opt == "XX")
        {
            onlyIfExists = true;
        }
        else if (opt == "KEEPTTL")
        {
            keepTtl = true;
            expireSec = 0;
            expireMs = 0;
            expireAtAbsMs = 0;
        }
        else if (opt == "GET")
        {
            returnOld = true;
        }
        else
        {
            return Protocol::formatError("syntax error in 'set' command");
        }
    }

    if (onlyIfNotExists && onlyIfExists)
    {
        return Protocol::formatError("NX and XX options at the same time are not compatible");
    }

    string oldFormatted = Protocol::formatNull();
    if (returnOld)
    {
        oldFormatted = handleGet({"GET", key});
    }

    bool keyExists = table.exists(key);
    if (onlyIfNotExists && keyExists)
    {
        return returnOld ? oldFormatted : Protocol::formatNull();
    }
    if (onlyIfExists && !keyExists)
    {
        return returnOld ? oldFormatted : Protocol::formatNull();
    }

    if (keepTtl && keyExists)
    {
        int64_t remMs = table.pttl(key);
        if (remMs > 0)
        {
            expireMs = remMs;
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
    else if (expireAtAbsMs > 0)
    {
        uint64_t now = FlatTable::getCurrentTimeMs();
        if (expireAtAbsMs <= now)
        {
            table.del(key);
            success = true;
        }
        else
        {
            success = table.set(key, valVariant, expireAtAbsMs);
        }
    }
    else
    {
        success = table.set(key, valVariant);
    }

    if (success)
    {
        return returnOld ? oldFormatted : Protocol::formatSimpleString("OK");
    }

    return Protocol::formatError("could not store key-value pair");
}

string CommandHandler::handleSetEx(const vector<string> &tokens)
{
    // Sintaxe padrão: SETEX key seconds value (também aceita SETEX key value seconds)
    if (tokens.size() != 4)
    {
        return Protocol::formatError("wrong number of arguments for 'setex' command");
    }

    int64_t sec = 0;
    if (parseStrictInt64(tokens[2], sec))
    {
        return handleSet({"SET", tokens[1], tokens[3], "EX", to_string(sec)});
    }
    if (parseStrictInt64(tokens[3], sec))
    {
        return handleSet({"SET", tokens[1], tokens[2], "EX", to_string(sec)});
    }
    return Protocol::formatError("value is not an integer or out of range");
}

string CommandHandler::handlePSetEx(const vector<string> &tokens)
{
    // Sintaxe padrão: PSETEX key milliseconds value (também aceita PSETEX key value milliseconds)
    if (tokens.size() != 4)
    {
        return Protocol::formatError("wrong number of arguments for 'psetex' command");
    }

    int64_t ms = 0;
    if (parseStrictInt64(tokens[2], ms))
    {
        return handleSet({"SET", tokens[1], tokens[3], "PX", to_string(ms)});
    }
    if (parseStrictInt64(tokens[3], ms))
    {
        return handleSet({"SET", tokens[1], tokens[2], "PX", to_string(ms)});
    }
    return Protocol::formatError("value is not an integer or out of range");
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
    if (tokens.size() == 2)
    {
        vector<string> incrByTokens = {tokens[0], tokens[1], "1"};
        return handleIncrBy(incrByTokens);
    }
    if (tokens.size() == 3)
    {
        vector<string> incrByTokens = {tokens[0], tokens[1], stripOptionBrackets(tokens[2])};
        return handleIncrBy(incrByTokens);
    }
    return Protocol::formatError("wrong number of arguments for 'incr' command");
}

string CommandHandler::handleIncrBy(const vector<string> &tokens)
{
    if (tokens.size() != 3)
    {
        return Protocol::formatError("wrong number of arguments for 'incrby' command");
    }

    const string &key = tokens[1];
    int64_t amount = 0;
    if (!parseStrictInt64(tokens[2], amount))
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
    if (tokens.size() == 2)
    {
        vector<string> decrByTokens = {tokens[0], tokens[1], "1"};
        return handleDecrBy(decrByTokens);
    }
    if (tokens.size() == 3)
    {
        vector<string> decrByTokens = {tokens[0], tokens[1], stripOptionBrackets(tokens[2])};
        return handleDecrBy(decrByTokens);
    }
    return Protocol::formatError("wrong number of arguments for 'decr' command");
}

string CommandHandler::handleDecrBy(const vector<string> &tokens)
{
    if (tokens.size() != 3)
    {
        return Protocol::formatError("wrong number of arguments for 'decrby' command");
    }

    const string &key = tokens[1];
    int64_t amount = 0;
    if (!parseStrictInt64(tokens[2], amount))
    {
        return Protocol::formatError("value is not an integer or out of range");
    }

    vector<string> incrByTokens = {"INCRBY", key, to_string(-amount)};
    return handleIncrBy(incrByTokens);
}

string CommandHandler::handleExpire(const vector<string> &tokens)
{
    // Sintaxe: EXPIRE key seconds [NX | XX | GT | LT]
    if (tokens.size() < 3 || tokens.size() > 4)
    {
        return Protocol::formatError("wrong number of arguments for 'expire' command");
    }

    int64_t seconds = 0;
    if (!parseStrictInt64(tokens[2], seconds))
    {
        return Protocol::formatError("value is not an integer or out of range");
    }

    string cond;
    if (tokens.size() == 4)
    {
        cond = toUpper(stripOptionBrackets(tokens[3]));
        if (!isValidExpireCondition(cond))
        {
            return Protocol::formatError("Unsupported option " + tokens[3]);
        }
        int64_t currentPttl = table.pttl(tokens[1]);
        if (currentPttl == -2)
        {
            return Protocol::formatInteger(0);
        }
        int64_t newRemainingMs = (seconds > 0) ? (seconds * 1000) : 0;
        if (!checkExpireCondition(currentPttl, newRemainingMs, cond))
        {
            return Protocol::formatInteger(0);
        }
    }

    bool ok = table.expire(tokens[1], seconds);
    return Protocol::formatInteger(ok ? 1 : 0);
}

string CommandHandler::handlePExpire(const vector<string> &tokens)
{
    // Sintaxe: PEXPIRE key milliseconds [NX | XX | GT | LT]
    if (tokens.size() < 3 || tokens.size() > 4)
    {
        return Protocol::formatError("wrong number of arguments for 'pexpire' command");
    }

    int64_t ms = 0;
    if (!parseStrictInt64(tokens[2], ms))
    {
        return Protocol::formatError("value is not an integer or out of range");
    }

    string cond;
    if (tokens.size() == 4)
    {
        cond = toUpper(stripOptionBrackets(tokens[3]));
        if (!isValidExpireCondition(cond))
        {
            return Protocol::formatError("Unsupported option " + tokens[3]);
        }
        int64_t currentPttl = table.pttl(tokens[1]);
        if (currentPttl == -2)
        {
            return Protocol::formatInteger(0);
        }
        int64_t newRemainingMs = (ms > 0) ? ms : 0;
        if (!checkExpireCondition(currentPttl, newRemainingMs, cond))
        {
            return Protocol::formatInteger(0);
        }
    }

    bool ok = table.pexpire(tokens[1], ms);
    return Protocol::formatInteger(ok ? 1 : 0);
}

string CommandHandler::handleExpireAt(const vector<string> &tokens)
{
    // Sintaxe: EXPIREAT key unix-time-seconds [NX | XX | GT | LT]
    if (tokens.size() < 3 || tokens.size() > 4)
    {
        return Protocol::formatError("wrong number of arguments for 'expireat' command");
    }

    int64_t unixSec = 0;
    if (!parseStrictInt64(tokens[2], unixSec))
    {
        return Protocol::formatError("value is not an integer or out of range");
    }

    if (unixSec <= 0)
    {
        bool ok = table.expire(tokens[1], 0);
        return Protocol::formatInteger(ok ? 1 : 0);
    }

    string cond;
    if (tokens.size() == 4)
    {
        cond = toUpper(stripOptionBrackets(tokens[3]));
        if (!isValidExpireCondition(cond))
        {
            return Protocol::formatError("Unsupported option " + tokens[3]);
        }
        int64_t currentPttl = table.pttl(tokens[1]);
        if (currentPttl == -2)
        {
            return Protocol::formatInteger(0);
        }
        uint64_t now = FlatTable::getCurrentTimeMs();
        uint64_t targetMs = static_cast<uint64_t>(unixSec) * 1000;
        int64_t newRemainingMs = (targetMs > now) ? static_cast<int64_t>(targetMs - now) : 0;
        if (!checkExpireCondition(currentPttl, newRemainingMs, cond))
        {
            return Protocol::formatInteger(0);
        }
    }

    bool ok = table.expireAt(tokens[1], static_cast<uint64_t>(unixSec));
    return Protocol::formatInteger(ok ? 1 : 0);
}

string CommandHandler::handlePExpireAt(const vector<string> &tokens)
{
    // Sintaxe: PEXPIREAT key unix-time-ms [NX | XX | GT | LT]
    if (tokens.size() < 3 || tokens.size() > 4)
    {
        return Protocol::formatError("wrong number of arguments for 'pexpireat' command");
    }

    int64_t unixMs = 0;
    if (!parseStrictInt64(tokens[2], unixMs))
    {
        return Protocol::formatError("value is not an integer or out of range");
    }

    if (unixMs <= 0)
    {
        bool ok = table.pexpire(tokens[1], 0);
        return Protocol::formatInteger(ok ? 1 : 0);
    }

    string cond;
    if (tokens.size() == 4)
    {
        cond = toUpper(stripOptionBrackets(tokens[3]));
        if (!isValidExpireCondition(cond))
        {
            return Protocol::formatError("Unsupported option " + tokens[3]);
        }
        int64_t currentPttl = table.pttl(tokens[1]);
        if (currentPttl == -2)
        {
            return Protocol::formatInteger(0);
        }
        uint64_t now = FlatTable::getCurrentTimeMs();
        uint64_t targetMs = static_cast<uint64_t>(unixMs);
        int64_t newRemainingMs = (targetMs > now) ? static_cast<int64_t>(targetMs - now) : 0;
        if (!checkExpireCondition(currentPttl, newRemainingMs, cond))
        {
            return Protocol::formatInteger(0);
        }
    }

    bool ok = table.pexpireAt(tokens[1], static_cast<uint64_t>(unixMs));
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
    // Sintaxe: RENAME key newkey [NX]
    if (tokens.size() < 3 || tokens.size() > 4)
    {
        return Protocol::formatError("wrong number of arguments for 'rename' command");
    }

    if (tokens.size() == 4)
    {
        string opt = toUpper(stripOptionBrackets(tokens[3]));
        if (opt == "NX")
        {
            return handleRenameNx({tokens[0], tokens[1], tokens[2]});
        }
        return Protocol::formatError("syntax error in 'rename' command");
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
    const SlabAllocator &alloc = table.getAllocator();
    string section = (tokens.size() >= 2) ? toUpper(stripOptionBrackets(tokens[1])) : "ALL";

    stringstream ss;
    if (section == "ALL" || section == "DEFAULT" || section == "STORAGE" || section == "KEYSPACE")
    {
        ss << "# Storage\r\n";
        ss << "active_keys:" << table.getActiveCount() << "\r\n";
        ss << "table_capacity:" << table.getCapacity() << "\r\n";
    }
    if (section == "ALL" || section == "DEFAULT" || section == "MEMORY")
    {
        ss << "# Memory\r\n";
        ss << "slab_block_size:" << alloc.getBlockSize() << "\r\n";
        ss << "slab_total_blocks:" << alloc.getTotalBlocks() << "\r\n";
        ss << "slab_allocated_blocks:" << alloc.getAllocatedBlocks() << "\r\n";
        ss << "slab_free_blocks:" << alloc.getFreeBlocks() << "\r\n";
        ss << "slab_chunks:" << alloc.getChunkCount() << "\r\n";
    }

    return Protocol::formatBulkString(ss.str());
}

string CommandHandler::handleHelp(const vector<string> &tokens)
{
    if (tokens.size() >= 2)
    {
        string target = toUpper(stripOptionBrackets(tokens[1]));
        if (target == "SET")
        {
            return Protocol::formatBulkString(
                "SET key value [EX seconds | PX milliseconds | EXAT timestamp | PXAT ms-timestamp | KEEPTTL] [NX | XX] [GET]\n"
                "  Define o valor de uma chave, com suporte a parametros opcionais de expiracao e condicao.");
        }
        if (target == "SETEX" || target == "PSETEX")
        {
            return Protocol::formatBulkString(
                "SETEX key seconds value\n"
                "PSETEX key milliseconds value\n"
                "  Define o valor e o tempo de expiracao de uma chave atomicamente.");
        }
        if (target == "EXPIRE" || target == "PEXPIRE" || target == "EXPIREAT" || target == "PEXPIREAT")
        {
            return Protocol::formatBulkString(
                "EXPIRE key seconds [NX | XX | GT | LT]\n"
                "PEXPIRE key milliseconds [NX | XX | GT | LT]\n"
                "EXPIREAT key unix-time-seconds [NX | XX | GT | LT]\n"
                "PEXPIREAT key unix-time-ms [NX | XX | GT | LT]\n"
                "  Define o tempo de vida (TTL) de uma chave existente.");
        }
        if (target == "INCR" || target == "INCRBY" || target == "DECR" || target == "DECRBY")
        {
            return Protocol::formatBulkString(
                "INCR key [amount]\n"
                "INCRBY key amount\n"
                "DECR key [amount]\n"
                "DECRBY key amount\n"
                "  Incrementa ou decrementa o valor inteiro de uma chave.");
        }
    }

    string helpText =
        "Available commands:\n"
        "  PING [message]\n"
        "  SET key value [EX seconds] [PX ms] [EXAT timestamp] [PXAT ms-timestamp] [NX | XX] [KEEPTTL] [GET]\n"
        "  SETEX key seconds value\n"
        "  PSETEX key milliseconds value\n"
        "  GET key\n"
        "  DEL key [key ...]\n"
        "  EXISTS key [key ...]\n"
        "  INCR key [amount]\n"
        "  INCRBY key amount\n"
        "  DECR key [amount]\n"
        "  DECRBY key amount\n"
        "  EXPIRE key seconds [NX | XX | GT | LT]\n"
        "  PEXPIRE key milliseconds [NX | XX | GT | LT]\n"
        "  EXPIREAT key timestamp [NX | XX | GT | LT]\n"
        "  PEXPIREAT key ms-timestamp [NX | XX | GT | LT]\n"
        "  TTL key\n"
        "  PTTL key\n"
        "  PERSIST key\n"
        "  RENAME key newkey [NX]\n"
        "  RENAMENX key newkey\n"
        "  PURGE\n"
        "  DUMP\n"
        "  DBSIZE\n"
        "  INFO [section]\n"
        "  HELP [command]\n"
        "  QUIT / EXIT";

    return Protocol::formatBulkString(helpText);
}
