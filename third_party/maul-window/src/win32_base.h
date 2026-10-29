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

// A performance counter's value in nanoseconds.
static inline uint64_t mwinWin32CounterNs(uint64_t counter)
{
    LARGE_INTEGER frequency;
    QueryPerformanceFrequency(&frequency);
    uint64_t seconds = counter / (uint64_t)frequency.QuadPart;
    uint64_t rest = counter % (uint64_t)frequency.QuadPart;
    return seconds * 1000000000u + rest * 1000000000u / (uint64_t)frequency.QuadPart;
}

// Nanoseconds on the performance counter.
static inline uint64_t mwinWin32Now(void)
{
    LARGE_INTEGER counter;
    QueryPerformanceCounter(&counter);
    return mwinWin32CounterNs((uint64_t)counter.QuadPart);
}

// When the input message being handled happened, on mwinWin32Now's
// clock. GetMessageTime counts milliseconds on GetTickCount's clock,
// which wraps every 49.7 days: the message's age is taken on that clock,
// in unsigned arithmetic across a wrap, and subtracted from now. Both
// tick in steps of 10 to 16 ms, so the time is within a step.
static inline uint64_t mwinWin32MessageTime(void)
{
    uint64_t now = mwinWin32Now();
    uint64_t age = (uint64_t)(DWORD)(GetTickCount() - (DWORD)GetMessageTime()) * 1000000u;
    return age < now ? now - age : now;
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
