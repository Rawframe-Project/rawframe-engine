// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The layer store: the nodes that root a layer, which the context holds
// and src/layer.c keeps in paint order.

#ifndef MAUL_UI_SRC_LAYER_STORE_H
#define MAUL_UI_SRC_LAYER_STORE_H

#include "maul-ui/base.h"
#include "maul-ui/focus.h"

#include <stdint.h>

// A node that roots a layer: its slot and generation, which tell a node
// destroyed since, when it became a layer or was raised, and what each
// player focused when it became one, outside it (its openers), which
// get the focus back when it closes.
typedef struct muiLayerEntry
{
    uint32_t slot;
    uint32_t generation;
    uint64_t activation;
    muiNodeId openers[MUI_MAX_PLAYERS];
} muiLayerEntry;

typedef struct muiLayerStore
{
    muiLayerEntry* entries;
    uint32_t count;
    uint32_t capacity;
    uint64_t activations;
} muiLayerStore;

static inline void muiLayerInit(muiLayerStore* store, muiLayerEntry* entries, uint32_t capacity)
{
    *store = (muiLayerStore){entries, 0, capacity, 0};
}

#endif // MAUL_UI_SRC_LAYER_STORE_H
