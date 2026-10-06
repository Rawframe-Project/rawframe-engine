// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Range values (record mui-0007): the context's table of ranges, one
// entry per node that is one, and what a pointer on it began with.

#ifndef MAUL_UI_SRC_RANGE_STORE_H
#define MAUL_UI_SRC_RANGE_STORE_H

#include "maul-ui/range.h"

#include <stdint.h>

typedef struct muiRangeEntry
{
    muiNodeId node;
    muiValueRange range;
    // Where the pointer holds the thumb along the axis, from its left or
    // top edge, and the value when the drag began.
    float grab;
    float startValue;
} muiRangeEntry;

typedef struct muiRangeStore
{
    muiRangeEntry* entries;
    uint32_t count;
    uint32_t capacity;
} muiRangeStore;

static inline void muiRangeInit(muiRangeStore* store, muiRangeEntry* entries, uint32_t capacity)
{
    *store = (muiRangeStore){.entries = entries, .capacity = capacity};
}

#endif // MAUL_UI_SRC_RANGE_STORE_H
