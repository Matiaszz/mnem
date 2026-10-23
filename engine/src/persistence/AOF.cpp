#include "headers/AOF.h"

using namespace std;

AOF::AOF(const string &path)
    : filePath(path),
      fileStream(path, ios::out | ios::app | ios::binary)
{
}

AOF::~AOF()
{
    if (fileStream.is_open())
    {
        fileStream.flush();
        fileStream.close();
    }
}

AOF::AOF(AOF &&other) noexcept
    : filePath(std::move(other.filePath)),
      fileStream(std::move(other.fileStream))
{
}

AOF &AOF::operator=(AOF &&other) noexcept
{
    if (this != &other)
    {
        if (fileStream.is_open())
        {
            fileStream.flush();
            fileStream.close();
        }
        filePath = std::move(other.filePath);
        fileStream = std::move(other.fileStream);
    }
    return *this;
}

bool AOF::append(string_view operation)
{
    if (operation.empty())
    {
        return false;
    }

    if (!fileStream.is_open())
    {
        fileStream.open(filePath, ios::out | ios::app | ios::binary);
        if (!fileStream.is_open())
        {
            return false;
        }
    }

    fileStream.write(operation.data(), static_cast<streamsize>(operation.size()));
    if (operation.back() != '\n')
    {
        fileStream.put('\n');
    }

    fileStream.flush();
    return fileStream.good();
}

bool AOF::clear()
{
    if (fileStream.is_open())
    {
        fileStream.close();
    }

    // Abre com ios::trunc para zerar o arquivo
    fileStream.open(filePath, ios::out | ios::trunc | ios::binary);
    if (!fileStream.is_open())
    {
        return false;
    }
    fileStream.close();

    // Reabre em modo append para as próximas escritas
    fileStream.open(filePath, ios::out | ios::app | ios::binary);
    return fileStream.is_open() && fileStream.good();
}
