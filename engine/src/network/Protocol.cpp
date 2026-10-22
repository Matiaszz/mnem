#include "headers/network/Protocol.h"
#include <sstream>
#include <cctype>

using namespace std;

Protocol::ParseStatus Protocol::parseCommand(string_view input, vector<string> &tokens, size_t &consumedBytes)
{
    tokens.clear();
    consumedBytes = 0;

    // Remove espaços em branco ou quebras de linha iniciais
    size_t start = 0;
    while (start < input.size() && (input[start] == '\r' || input[start] == '\n' || input[start] == ' ' || input[start] == '\t'))
    {
        ++start;
    }

    if (start >= input.size())
    {
        consumedBytes = start;
        return ParseStatus::INCOMPLETE;
    }

    string_view remaining = input.substr(start);
    size_t subConsumed = 0;
    ParseStatus status = ParseStatus::INCOMPLETE;

    if (remaining[0] == '*')
    {
        status = parseArrayCommand(remaining, tokens, subConsumed);
    }
    else
    {
        status = parseInlineCommand(remaining, tokens, subConsumed);
    }

    if (status == ParseStatus::OK)
    {
        consumedBytes = start + subConsumed;
    }
    else if (status == ParseStatus::ERROR)
    {
        // Se houver erro de sintaxe, consome até a próxima quebra de linha para recuperação
        size_t nextNl = remaining.find('\n');
        if (nextNl != string_view::npos)
        {
            consumedBytes = start + nextNl + 1;
        }
        else
        {
            consumedBytes = input.size();
        }
    }

    return status;
}

Protocol::ParseStatus Protocol::parseInlineCommand(string_view input, vector<string> &tokens, size_t &consumedBytes)
{
    size_t lineEnd = input.find('\n');
    if (lineEnd == string_view::npos)
    {
        return ParseStatus::INCOMPLETE;
    }

    consumedBytes = lineEnd + 1;
    string_view line = input.substr(0, lineEnd);
    if (!line.empty() && line.back() == '\r')
    {
        line.remove_suffix(1);
    }

    string current;
    bool inDoubleQuotes = false;
    bool inSingleQuotes = false;
    bool escaping = false;

    for (size_t i = 0; i < line.size(); ++i)
    {
        char c = line[i];

        if (escaping)
        {
            current += c;
            escaping = false;
            continue;
        }

        if (c == '\\' && (inDoubleQuotes || inSingleQuotes))
        {
            escaping = true;
            continue;
        }

        if (c == '"' && !inSingleQuotes)
        {
            inDoubleQuotes = !inDoubleQuotes;
            continue;
        }

        if (c == '\'' && !inDoubleQuotes)
        {
            inSingleQuotes = !inSingleQuotes;
            continue;
        }

        if (isspace(static_cast<unsigned char>(c)) && !inDoubleQuotes && !inSingleQuotes)
        {
            if (!current.empty())
            {
                tokens.push_back(std::move(current));
                current.clear();
            }
        }
        else
        {
            current += c;
        }
    }

    if (inDoubleQuotes || inSingleQuotes)
    {
        return ParseStatus::ERROR;
    }

    if (!current.empty())
    {
        tokens.push_back(std::move(current));
    }

    if (tokens.empty())
    {
        return ParseStatus::INCOMPLETE;
    }

    return ParseStatus::OK;
}

Protocol::ParseStatus Protocol::parseArrayCommand(string_view input, vector<string> &tokens, size_t &consumedBytes)
{
    // Formato: *<num_elements>\r\n$<len>\r\n<dados>\r\n...
    size_t firstNl = input.find('\n');
    if (firstNl == string_view::npos)
    {
        return ParseStatus::INCOMPLETE;
    }

    string_view countLine = input.substr(1, firstNl - 1);
    if (!countLine.empty() && countLine.back() == '\r')
    {
        countLine.remove_suffix(1);
    }

    int elementCount = 0;
    try
    {
        elementCount = stoi(string(countLine));
    }
    catch (...)
    {
        return ParseStatus::ERROR;
    }

    if (elementCount <= 0)
    {
        return ParseStatus::ERROR;
    }

    size_t offset = firstNl + 1;
    for (int i = 0; i < elementCount; ++i)
    {
        if (offset >= input.size())
        {
            return ParseStatus::INCOMPLETE;
        }

        if (input[offset] != '$')
        {
            return ParseStatus::ERROR;
        }

        size_t lenNl = input.find('\n', offset);
        if (lenNl == string_view::npos)
        {
            return ParseStatus::INCOMPLETE;
        }

        string_view lenLine = input.substr(offset + 1, lenNl - (offset + 1));
        if (!lenLine.empty() && lenLine.back() == '\r')
        {
            lenLine.remove_suffix(1);
        }

        int itemLen = 0;
        try
        {
            itemLen = stoi(string(lenLine));
        }
        catch (...)
        {
            return ParseStatus::ERROR;
        }

        if (itemLen < 0)
        {
            return ParseStatus::ERROR;
        }

        offset = lenNl + 1;
        // Espera itemLen bytes + \r\n ou \n
        if (offset + itemLen > input.size())
        {
            return ParseStatus::INCOMPLETE;
        }

        string token(input.substr(offset, itemLen));
        tokens.push_back(std::move(token));
        offset += itemLen;

        // Pula o \r\n após o item
        if (offset < input.size() && input[offset] == '\r')
        {
            offset++;
        }
        if (offset < input.size() && input[offset] == '\n')
        {
            offset++;
        }
        else
        {
            // Se ainda não recebeu o delimitador final do item
            if (offset >= input.size())
            {
                return ParseStatus::INCOMPLETE;
            }
        }
    }

    consumedBytes = offset;
    return ParseStatus::OK;
}

string Protocol::formatSimpleString(string_view str)
{
    string res = "+";
    res.append(str);
    res.append("\r\n");
    return res;
}

string Protocol::formatError(string_view err)
{
    string res = "-ERR ";
    res.append(err);
    res.append("\r\n");
    return res;
}

string Protocol::formatInteger(int64_t val)
{
    string res = ":";
    res.append(to_string(val));
    res.append("\r\n");
    return res;
}

string Protocol::formatBulkString(string_view val)
{
    string res = "$";
    res.append(to_string(val.size()));
    res.append("\r\n");
    res.append(val);
    res.append("\r\n");
    return res;
}

string Protocol::formatNull()
{
    return "$-1\r\n";
}

string Protocol::formatArray(const vector<string> &elements)
{
    string res = "*" + to_string(elements.size()) + "\r\n";
    for (const auto &elem : elements)
    {
        res.append(formatBulkString(elem));
    }
    return res;
}
