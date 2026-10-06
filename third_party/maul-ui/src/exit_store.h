// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Exit transitions (record mui-0007): the context's table of nodes
// exiting.

#ifndef MAUL_UI_SRC_EXIT_STORE_H
#define MAUL_UI_SRC_EXIT_STORE_H

#include "maul-ui/base.h"

#include <stdbool.h>
#include <stdint.h>

typedef struct muiExitEntry
{
    muiNodeId node;
    // Whether its finish was reported.
    bool finished;
} muiExitEntry;

typedef struct muiExitStore
{
    muiExitEntry* entries;
    uint32_t count;
    uint32_t capacity;
} muiExitStore;

static inline void muiExitInit(muiExitStore* store, muiExitEntry* entries, uint32_t capacity)
{
    *store = (muiExitStore){.entries = entries, .capacity = capacity};
}

#endif // MAUL_UI_SRC_EXIT_STORE_H
