// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The context's parts, which the modules of the public API share.

#ifndef MAUL_UI_SRC_CONTEXT_H
#define MAUL_UI_SRC_CONTEXT_H

#include "animation.h"
#include "draw_store.h"
#include "inherit.h"
#include "layer_store.h"
#include "layout_node.h"
#include "notify.h"
#include "style_store.h"
#include "theme_store.h"
#include "token_store.h"
#include "tree.h"

#include "maul-ui/context.h"

struct muiContext
{
    muiAllocator allocator;
    // The size of the one block that holds the context and its slots.
    size_t blockSize;
    muiTree tree;
    // Layout's values per node, parallel to the tree's slots.
    muiLayoutNode* layout;
    // Resolved visual values per node, parallel to the tree's slots: paint
    // reads them, layout never.
    muiVisualStyle* visual;
    // Resolved text values per node, and what each node's text computes
    // to with its parent's, parallel to the tree's slots.
    muiTextStyle* text;
    muiTextRecord* textRecords;
    // A node's resolved interaction values.
    muiInteractionStyle* interaction;
    // Whether a class, a token name or a direct write has ever given a
    // text property: until then every node's text is the defaults, and
    // the style pass leaves the records alone.
    bool textGiven;
    muiStyleStore style;
    muiTokenStore tokens;
    muiThemeStore themes;
    muiDrawStore draw;
    // The nodes that root a layer, in paint order.
    muiLayerStore layers;
    // What conditions read of the world outside the tree.
    muiEnvironment environment;
    // How many times the host edited a class, a node type or the
    // environment, each of which restyles every node.
    uint32_t styleEdits;
    muiNotifyQueue notifications;
    // Transition specs and running transitions, and the latest time a
    // layout run was given.
    muiAnimationStore animations;
    uint64_t lastTimeNs;
    uint64_t misuse;
    // Set while a measure or paint function runs; edits are refused then.
    bool inHostCall;
};

// Counts one refused call and returns mui_errorInvalid for it.
muiResult muiRefuse(muiContext* context);

// Whether an edit must be refused because a measure function is running.
bool muiIsInHostCall(const muiContext* context);

// The slot of a live node for an edit, or 0 with the status to return in
// statusOut: misuse for the null id or an edit from a measure function,
// stale for a gone node.
uint32_t muiResolveEdit(muiContext* context, muiNodeId nodeId, muiResult* statusOut);

#endif // MAUL_UI_SRC_CONTEXT_H
