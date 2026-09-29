// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// What every Win32 module of the backend uses: the clock, and pointers
// carried in Win32's integer types.

#ifndef MAUL_WINDOW_SRC_WIN32_BASE_H
#define MAUL_WINDOW_SRC_WIN32_BASE_H

#include <stdint.h>
#include <string.h>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

// Nanoseconds on the performance counter.
static inline uint64_t mwinWin32Now(void)
{
    LARGE_INTEGER counter;
    LARGE_INTEGER frequency;
    QueryPerformanceCounter(&counter);
    QueryPerformanceFrequency(&frequency);
    uint64_t seconds = (uint64_t)counter.QuadPart / (uint64_t)frequency.QuadPart;
    uint64_t rest = (uint64_t)counter.QuadPart % (uint64_t)frequency.QuadPart;
    return seconds * 1000000000u + rest * 1000000000u / (uint64_t)frequency.QuadPart;
}

// The pointer a message parameter or window long carries. Win32 passes
// pointers through its integer types; the bytes are copied, not cast.
static inline void* mwinWin32Pointer(LONG_PTR value)
{
    void* pointer = nullptr;
    memcpy((void*)&pointer, (const void*)&value, sizeof(pointer));
    return pointer;
}

#endif // MAUL_WINDOW_SRC_WIN32_BASE_H
