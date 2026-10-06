// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The focus store (record mui-0007): each player's focused node and
// whether its focus is shown, which the context holds and src/focus.c
// keeps.

#ifndef MAUL_UI_SRC_FOCUS_STORE_H
#define MAUL_UI_SRC_FOCUS_STORE_H

#include "maul-ui/focus.h"

#include <stdbool.h>

// A node's link to where directional navigation goes from it.
typedef struct muiNeighbor
{
    uint32_t slot;
    uint32_t generation;
    muiDirection direction;
    muiNodeId target;
} muiNeighbor;

typedef struct muiFocusStore
{
    muiNodeId nodes[MUI_MAX_PLAYERS];
    // Whether a focus moved by code would be shown: false after a
    // pointer moved it.
    bool showsByCode[MUI_MAX_PLAYERS];
    // The players that focus a node, as bits.
    uint8_t holders;
    // Directional links, in no order; those of destroyed nodes are taken
    // out when room is needed.
    muiNeighbor* neighbors;
    uint32_t neighborCount;
    uint32_t neighborCapacity;
} muiFocusStore;

static inline void muiFocusInit(muiFocusStore* store, muiNeighbor* neighbors, uint32_t capacity)
{
    *store = (muiFocusStore){.neighbors = neighbors, .neighborCapacity = capacity};
    for (int i = 0; i < MUI_MAX_PLAYERS; i++)
    {
        store->showsByCode[i] = true;
    }
}

#endif // MAUL_UI_SRC_FOCUS_STORE_H
