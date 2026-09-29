// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The monotonic clock of the POSIX backends, and the millisecond
// timestamps Wayland and the X server stamp input with: 32 bits of the
// same clock on Linux.

#ifndef MAUL_WINDOW_SRC_MONOTONIC_H
#define MAUL_WINDOW_SRC_MONOTONIC_H

#include <stdint.h>
#include <time.h>

// Nanoseconds on the monotonic clock.
static inline uint64_t mwinMonotonicNow(void)
{
    struct timespec now;
    (void)clock_gettime(CLOCK_MONOTONIC, &now);
    return (uint64_t)now.tv_sec * 1000000000u + (uint64_t)now.tv_nsec;
}

// A 32-bit millisecond timestamp in nanoseconds: the time of the latest
// wrap of the millisecond count before now. A time that seems far off
// is on another clock and taken as now.
static inline uint64_t mwinMonotonicFromMilliseconds(uint32_t milliseconds)
{
    uint64_t now = mwinMonotonicNow();
    uint32_t age = (uint32_t)(now / 1000000u) - milliseconds;
    return age < 60000u && (uint64_t)age * 1000000u <= now ? now - (uint64_t)age * 1000000u : now;
}

#endif // MAUL_WINDOW_SRC_MONOTONIC_H
