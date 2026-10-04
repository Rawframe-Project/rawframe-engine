// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The host clock is each platform's monotonic clock, the one its audio
// services stamp with. A stamp is written between two increments of a
// sequence counter, so a reader that sees the same even count before
// and after has a whole stamp.

#include "clock.h"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#elif defined(__EMSCRIPTEN__)
#include <emscripten/emscripten.h>
#else
#include <time.h>
#endif

int64_t maudNowNanoseconds(void)
{
#if defined(_WIN32)
    LARGE_INTEGER counter;
    LARGE_INTEGER frequency;
    QueryPerformanceCounter(&counter);
    QueryPerformanceFrequency(&frequency);
    int64_t seconds = counter.QuadPart / frequency.QuadPart;
    int64_t rest = counter.QuadPart % frequency.QuadPart;
    return seconds * 1000000000 + rest * 1000000000 / frequency.QuadPart;
#elif defined(__EMSCRIPTEN__)
    return (int64_t)(emscripten_get_now() * 1e6);
#elif defined(__APPLE__)
    // The HAL's host time, mach_absolute_time, in nanoseconds.
    return (int64_t)clock_gettime_nsec_np(CLOCK_UPTIME_RAW);
#else
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (int64_t)now.tv_sec * 1000000000 + now.tv_nsec;
#endif
}

static void Stamp(maudStreamCore* core, int64_t host, int64_t latency)
{
    uint64_t position = atomic_load_explicit(&core->position, memory_order_relaxed);
    uint32_t sequence = atomic_load_explicit(&core->clockSequence, memory_order_relaxed);
    atomic_store_explicit(&core->clockSequence, sequence + 1, memory_order_relaxed);
    atomic_thread_fence(memory_order_release);
    atomic_store_explicit(&core->clockPosition, position, memory_order_relaxed);
    atomic_store_explicit(&core->clockHost, host, memory_order_relaxed);
    atomic_store_explicit(&core->clockLatency, latency, memory_order_relaxed);
    atomic_store_explicit(&core->clockSequence, sequence + 2, memory_order_release);
}

void maudResetClock(maudStreamCore* core)
{
    Stamp(core, 0, 0);
}

void maudStampOutputClock(maudStreamCore* core, int64_t latency)
{
    Stamp(core, maudNowNanoseconds() + latency, latency);
}

void maudStampInputClock(maudStreamCore* core, int64_t latency)
{
    Stamp(core, maudNowNanoseconds() - latency, latency);
}

void maudReadClock(const maudStreamCore* core, uint64_t* position, int64_t* host, int64_t* latency)
{
    uint32_t before = 0;
    uint32_t after = 0;
    do
    {
        before = atomic_load_explicit(&core->clockSequence, memory_order_acquire);
        *position = atomic_load_explicit(&core->clockPosition, memory_order_relaxed);
        *host = atomic_load_explicit(&core->clockHost, memory_order_relaxed);
        *latency = atomic_load_explicit(&core->clockLatency, memory_order_relaxed);
        atomic_thread_fence(memory_order_acquire);
        after = atomic_load_explicit(&core->clockSequence, memory_order_relaxed);
    } while ((before & 1u) != 0 || before != after);
}
