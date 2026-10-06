// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Virtualization (record mui-0007): the context's virtual lists, their
// items' extents and the prefix sums over them, and each node's item.

#ifndef MAUL_UI_SRC_VIRTUAL_STORE_H
#define MAUL_UI_SRC_VIRTUAL_STORE_H

#include "maul-ui/virtual.h"

#include <stdbool.h>
#include <stdint.h>

typedef struct muiVirtualEntry
{
    muiNodeId node;
    muiVirtualList list;
    // An estimated list's items: sizes[base + i] is item i's extent, and
    // sums[base ..] a Fenwick tree over extent plus gap, so an item's
    // offset and the item at an offset take O(log count).
    uint32_t base;
    // The window last found, and whether one was.
    uint32_t first;
    uint32_t end;
    bool windowed;
    // Set anew: the node of the item shown first, and how far past the
    // viewport's start it began, which the next layout keeps.
    bool anchoring;
    muiNodeId anchor;
    double anchorShift;
} muiVirtualEntry;

// A node bound to an item since removed: out of the flow, placed nowhere.
#define MUI_ITEM_REMOVED UINT32_MAX

typedef struct muiVirtualStore
{
    muiVirtualEntry* entries;
    uint32_t count;
    uint32_t capacity;
    float* sizes;
    double* sums;
    uint32_t itemCapacity;
    // Per node: its item's index plus 1, 0 for none, MUI_ITEM_REMOVED.
    uint32_t* items;
} muiVirtualStore;

#endif // MAUL_UI_SRC_VIRTUAL_STORE_H
