
#pragma once

#include <cstddef>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <sys/mman.h>
#endif

namespace mnem::platform
{

    inline void *allocate_anonymous(std::size_t size)
    {
#ifdef _WIN32
        return VirtualAlloc(
            nullptr,
            size,
            MEM_RESERVE | MEM_COMMIT,
            PAGE_READWRITE);
#else
        void *memory = mmap(
            nullptr,
            size,
            PROT_READ | PROT_WRITE,
            MAP_PRIVATE | MAP_ANONYMOUS,
            -1,
            0);

        return memory == MAP_FAILED ? nullptr : memory;
#endif
    }

    inline bool free_anonymous(void *memory, std::size_t size)
    {
        if (!memory)
            return true;

#ifdef _WIN32
        return VirtualFree(memory, 0, MEM_RELEASE) != 0;
#else
        return munmap(memory, size) == 0;
#endif
    }

}
