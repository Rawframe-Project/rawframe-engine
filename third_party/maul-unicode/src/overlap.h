// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Whether a caller's output overlaps an input of its type, which the calls
// that write while they read refuse. The addresses are compared as
// integers, since the two arrays may be different objects, and through the
// distance between their starts, so no end is computed that could wrap.

#ifndef MAUL_UNICODE_SRC_OVERLAP_H
#define MAUL_UNICODE_SRC_OVERLAP_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Whether count1 elements at one and count2 at two, each size bytes,
// share a byte. An empty array overlaps nothing.
static inline bool muniOverlaps(const void* one, size_t count1, const void* two, size_t count2,
                                size_t size)
{
    uintptr_t a = (uintptr_t)one;
    uintptr_t b = (uintptr_t)two;
    if (count1 == 0 || count2 == 0)
    {
        return false;
    }
    return b >= a ? (b - a) / size < count1 : (a - b) / size < count2;
}

#endif // MAUL_UNICODE_SRC_OVERLAP_H
