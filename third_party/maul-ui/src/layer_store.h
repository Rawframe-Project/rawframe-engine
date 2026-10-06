// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The layer store (record mui-0007): the nodes that root a layer, which
// the context holds and src/layer.c keeps in paint order.

#ifndef MAUL_UI_SRC_LAYER_STORE_H
#define MAUL_UI_SRC_LAYER_STORE_H

#include <stdint.h>

// A node that roots a layer: its slot and generation, which tell a node
// destroyed since, and when it became a layer or was raised.
typedef struct muiLayerEntry
{
    uint32_t slot;
    uint32_t generation;
    uint64_t activation;
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
