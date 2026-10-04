// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Sorting 64-bit keys in linear time.

#include "sort.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

size_t mnavSortUnique(uint64_t* keys, uint64_t* scratch, size_t count)
{
    // Eight stable counting passes, lowest byte first. A pass whose byte
    // is the same in every key moves nothing and is skipped.
    uint64_t* from = keys;
    uint64_t* to = scratch;
    for (int32_t shift = 0; shift < 64; shift += 8)
    {
        size_t counts[257] = {0};
        for (size_t i = 0; i < count; ++i)
        {
            counts[((from[i] >> shift) & 0xFFu) + 1] += 1;
        }
        bool trivial = false;
        for (size_t b = 1; b <= 256; ++b)
        {
            trivial = trivial || counts[b] == count;
        }
        if (trivial)
        {
            continue;
        }
        for (size_t b = 0; b < 256; ++b)
        {
            counts[b + 1] += counts[b];
        }
        for (size_t i = 0; i < count; ++i)
        {
            to[counts[(from[i] >> shift) & 0xFFu]++] = from[i];
        }
        uint64_t* swap = from;
        from = to;
        to = swap;
    }
    if (from != keys && count > 0)
    {
        memcpy(keys, from, count * sizeof(uint64_t));
    }
    size_t unique = 0;
    for (size_t i = 0; i < count; ++i)
    {
        if (unique == 0 || keys[unique - 1] != keys[i])
        {
            keys[unique++] = keys[i];
        }
    }
    return unique;
}
