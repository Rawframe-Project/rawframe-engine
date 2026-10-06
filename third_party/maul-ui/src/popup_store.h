// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Popups (record mui-0007): the context's table of popups, one entry per
// node that is one.

#ifndef MAUL_UI_SRC_POPUP_STORE_H
#define MAUL_UI_SRC_POPUP_STORE_H

#include "maul-ui/popup.h"

#include <stdbool.h>
#include <stdint.h>

typedef struct muiPopupEntry
{
    muiNodeId node;
    muiPopup popup;
    // The side the last placement used, and whether there was one.
    muiPopupSide placedSide;
    bool placed;
    // Whether it was dismissed since it was set, and when it was set, a
    // count of sets.
    bool dismissed;
    uint32_t serial;
    // While placing: whether it is done, and the index of the entry
    // holding its anchor, placed first, or the count for none.
    uint8_t mark;
    uint32_t holder;
    // While dismissing: how many popups it nests under.
    uint32_t depth;
} muiPopupEntry;

typedef struct muiPopupStore
{
    muiPopupEntry* entries;
    uint32_t count;
    uint32_t capacity;
    // The sets so far, which order popups for Escape.
    uint32_t serial;
} muiPopupStore;

static inline void muiPopupInit(muiPopupStore* store, muiPopupEntry* entries, uint32_t capacity)
{
    *store = (muiPopupStore){.entries = entries, .capacity = capacity};
}

#endif // MAUL_UI_SRC_POPUP_STORE_H
