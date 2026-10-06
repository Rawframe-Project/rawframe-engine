// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Layers (record mui-0007): the nodes that root one, in paint order from
// the bottom: activation layers in the order they became layers or were
// raised, then the overlay band in the same order.

#ifndef MAUL_UI_SRC_LAYER_H
#define MAUL_UI_SRC_LAYER_H

#include "layer_store.h"
#include "tree.h"

#include "maul-ui/interaction.h"

#include <stdbool.h>
#include <stdint.h>

typedef struct muiContext muiContext;

// Takes note of a node's layer kind after it may have changed from
// before: a node that became a layer is the latest activated, one that
// stopped is forgotten, and either is painted again.
void muiNoteLayer(muiContext* context, uint32_t slot, muiLayerKind before);

// Whether a node roots a layer: it has a layer kind and the store holds
// it.
static inline bool muiIsLayerRoot(const muiTree* tree, uint32_t slot)
{
    return muiTreeAt(tree, slot)->apart;
}

// The slot of the layer at a place from the bottom, or 0 when the node
// there is gone or roots no layer any more.
uint32_t muiLayerAt(const muiContext* context, uint32_t place);

#endif // MAUL_UI_SRC_LAYER_H
