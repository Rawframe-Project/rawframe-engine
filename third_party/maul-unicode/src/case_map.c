// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The full case mappings of special code points, which sit in a sorted
// table with their mappings in a pool of UTF-16 units.

#include "case_map.h"

// The entry of a special code point in the special table gives the
// lengths of its mappings, which follow each other in the pool.
size_t muniMapCaseSpecial(uint32_t codePoint, int kind, uint32_t* out)
{
    uint32_t low = 0;
    uint32_t high = muniCaseSpecialCount;
    while (high - low > 1)
    {
        uint32_t middle = (low + high) / 2;
        if (muniCaseSpecials[middle] >> 8 <= codePoint)
        {
            low = middle;
        }
        else
        {
            high = middle;
        }
    }
    uint32_t entry = muniCaseSpecials[low];
    size_t unit = muniCaseSpecialOffsets[low];
    size_t written = 0;
    for (int k = 0; k <= kind; k++)
    {
        uint32_t count = entry >> (2 * k) & 3;
        for (uint32_t i = 0; i < count; i++)
        {
            uint32_t value = muniCaseSpecialPool[unit++];
            if (value - 0xD800 < 0x400)
            {
                value = 0x10000 + ((value - 0xD800) << 10) + (muniCaseSpecialPool[unit++] - 0xDC00);
            }
            if (k == kind)
            {
                out[written++] = value;
            }
        }
    }
    return written;
}
