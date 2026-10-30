// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The context's parts, which the modules of the public API share.

#ifndef MAUL_UI_SRC_CONTEXT_H
#define MAUL_UI_SRC_CONTEXT_H

#include "access_store.h"
#include "animation.h"
#include "draw_store.h"
#include "event_store.h"
#include "exit_store.h"
#include "focus_store.h"
#include "inherit.h"
#include "layer_store.h"
#include "layout_node.h"
#include "notify.h"
#include "pointer_store.h"
#include "popup_store.h"
#include "range_store.h"
#include "scroll_store.h"
#include "style_store.h"
#include "theme_store.h"
#include "token_store.h"
#include "tree.h"
#include "virtual_store.h"

#include "maul-ui/context.h"

struct muiContext
{
    muiAllocator allocator;
    // The size of the one block that holds the context and its slots.
    size_t blockSize;
    muiTree tree;
    // Layout's values per node, parallel to the tree's slots.
    muiLayoutNode* layout;
    // The padding each node was last laid out with: its style's, raised
    // by the safe area on the edges it names; and the safe area last
    // given (muiLayoutInput).
    muiEdges* paddings;
    muiSides safeArea;
    // Resolved visual values per node, parallel to the tree's slots: paint
    // reads them, layout never.
    muiVisualStyle* visual;
    // Resolved text values per node, and what each node's text computes
    // to with its parent's, parallel to the tree's slots.
    muiTextStyle* text;
    muiTextRecord* textRecords;
    // A node's resolved interaction values.
    muiInteractionStyle* interaction;
    // Each node's scroll offset and extent, and whether an offset moved
    // since the last draw list.
    muiScrollState* scrolls;
    bool scrolled;
    // The scroll rule and the wheel's latch.
    muiScrollStore scrolling;
    // The nodes that are ranges.
    muiRangeStore ranges;
    // The nodes that are popups.
    muiPopupStore popups;
    // The nodes exiting.
    muiExitStore exits;
    // The virtual lists, their items' extents, and each node's item.
    muiVirtualStore lists;
    // The host's accessibility data, the roots updates are built for,
    // and what was last sent.
    muiAccessStore access;
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
    // The pointers known and their records.
    muiPointerStore pointers;
    // Each player's focus.
    muiFocusStore focus;
    // The host's event function and the route under way.
    muiEventStore events;
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
    // The work done since the context was made (muiContext_GetWorkCounts).
    muiWorkCounts work;
    // Set while one of the context's functions that change it runs: host
    // code it calls then may not call back in (Maul conventions, Threads).
    // Dispatch clears it while the event function runs.
    bool busy;
};

// Counts one refused call and returns mui_errorInvalid for it.
muiResult muiRefuse(muiContext* context);

// Counts one argument past a fixed bound and returns mui_errorLimit for
// it.
muiResult muiRefuseLimit(muiContext* context);

// Counts one refused argument and returns status, mui_errorInvalid or
// mui_errorLimit, for it.
static inline muiResult muiRefuseAs(muiContext* context, muiResult status)
{
    return status == mui_errorLimit ? muiRefuseLimit(context) : muiRefuse(context);
}

// Counts one call back in and returns mui_errorBusy for it.
muiResult muiRefuseBusy(muiContext* context);

// Marks the context busy for one of its functions that change it:
// mui_success, or mui_errorBusy with one misuse counted when it is busy
// already. A NULL context is left to the function.
muiResult muiEnter(muiContext* context);

// Ends what muiEnter began.
void muiLeave(muiContext* context);

// The slot of a live node for an edit, or 0 with the status to return in
// statusOut: misuse for the null id, stale for a gone node.
uint32_t muiResolveEdit(muiContext* context, muiNodeId nodeId, muiResult* statusOut);

// Marks a node for the accessibility tree after a change no other stage
// marks: only while a root builds updates, as enabling one sends every
// node.
static inline void muiNoteAccess(muiContext* context, uint32_t slot)
{
    if (context->access.rootCount != 0)
    {
        muiTreeMark(&context->tree, slot, mui_stageAccess);
    }
}

// A scroll container's offset moved: the draw list's transforms, and its
// own and its children's places for the accessibility tree.
static inline void muiNoteScrolled(muiContext* context, uint32_t slot)
{
    context->scrolled = true;
    if (context->access.rootCount != 0)
    {
        muiTreeMarkWithChildren(&context->tree, slot, mui_stageAccess);
    }
}

#endif // MAUL_UI_SRC_CONTEXT_H
