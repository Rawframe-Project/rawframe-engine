// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Focus (record mui-0007). Each player focuses at most one node; the node
// keeps the players focusing it, and those showing it, as bits its
// states read. Sequential navigation walks the layer holding the focus
// once, keeping the nearest node on each side of it in the order.

#include "maul-ui/focus.h"

#include "context.h"
#include "focus.h"
#include "layer.h"
#include "notify.h"
#include "popup.h"
#include "scroll.h"
#include "style_store.h"
#include "tree.h"

#include "maul-ui/interaction.h"

static bool IsNull(muiNodeId nodeId)
{
    return nodeId.index1 == 0;
}

static uint32_t SlotOf(const muiContext* context, muiNodeId nodeId)
{
    return IsNull(nodeId) ? 0 : muiTreeResolve(&context->tree, nodeId);
}

bool muiFocusTakes(const muiContext* context, uint32_t slot, muiFocusMode least)
{
    const muiState off = mui_stateDisabled | mui_stateExiting;
    muiFocusMode mode = context->interaction[slot - 1].focusMode;
    // least is never mui_focusNone.
    return mode >= least && (context->style.nodes[slot - 1].states & off) == 0;
}

static uint32_t TopOf(const muiTree* tree, uint32_t slot)
{
    while (muiTreeAt(tree, slot)->links.parent != 0)
    {
        slot = muiTreeAt(tree, slot)->links.parent;
    }
    return slot;
}

bool muiFocusIsCovered(const muiContext* context, uint32_t slot)
{
    const muiTree* tree = &context->tree;
    if (muiTreeIsExiting(tree, slot))
    {
        return true;
    }
    uint32_t top = 0;
    for (uint32_t i = context->layers.count; i > 0; i--)
    {
        uint32_t layer = muiLayerAt(context, i - 1);
        if (layer == 0 || muiTreeIsExiting(tree, layer))
        {
            continue;
        }
        if (muiTreeIsAncestor(tree, layer, slot))
        {
            return false;
        }
        if (context->interaction[layer - 1].layer == mui_layerModal)
        {
            top = top != 0 ? top : TopOf(tree, slot);
            if (TopOf(tree, layer) == top)
            {
                return true;
            }
        }
    }
    return false;
}

static void Notify(muiContext* context, muiNotificationKind kind, muiNodeId nodeId, uint8_t player)
{
    const muiNotification record = {kind, nodeId, player};
    muiNotifyPost(&context->notifications, &record);
}

// Writes a player's bits on a node, restyling it when they change.
static void SetBits(muiContext* context, uint32_t slot, uint8_t player, bool focused, bool shown)
{
    muiNodeStyle* style = &context->style.nodes[slot - 1];
    uint8_t bit = (uint8_t)(1u << player);
    uint8_t focusedBy = (uint8_t)(focused ? style->focusedBy | bit : style->focusedBy & ~bit);
    uint8_t shownBy = (uint8_t)(shown ? style->shownBy | bit : style->shownBy & ~bit);
    if (focusedBy != style->focusedBy || shownBy != style->shownBy)
    {
        style->focusedBy = focusedBy;
        style->shownBy = shownBy;
        style->edited = true;
        muiTreeMark(&context->tree, slot, mui_stageStyle);
    }
}

void muiFocusAssign(muiContext* context, uint8_t player, uint32_t slot, bool shown)
{
    muiFocusStore* store = &context->focus;
    muiNodeId was = store->nodes[player];
    uint32_t wasSlot = SlotOf(context, was);
    if (wasSlot != 0)
    {
        SetBits(context, wasSlot, player, false, false);
        if (wasSlot != slot)
        {
            Notify(context, mui_notificationFocusLost, was, player);
        }
    }
    store->nodes[player] = (muiNodeId){0, 0};
    store->holders &= (uint8_t)~(1u << player);
    if (slot != 0)
    {
        SetBits(context, slot, player, true, shown);
        store->nodes[player] = muiTreeIdOf(&context->tree, slot);
        store->holders |= (uint8_t)(1u << player);
        if (wasSlot != slot)
        {
            Notify(context, mui_notificationFocusGained, store->nodes[player], player);
        }
    }
}

muiResult muiFocus_Set(muiContext* context, uint8_t player, muiNodeId nodeId, muiFocusCause cause)
{
    if (context == nullptr)
    {
        return mui_errorInvalid;
    }
    if (player >= MUI_MAX_PLAYERS || cause > mui_focusByNavigation || muiIsInHostCall(context))
    {
        return muiRefuse(context);
    }
    uint32_t slot = 0;
    if (!IsNull(nodeId))
    {
        muiResult status = mui_success;
        slot = muiResolveEdit(context, nodeId, &status);
        if (slot == 0)
        {
            return status;
        }
        if (!muiFocusTakes(context, slot, mui_focusPointer) || muiFocusIsCovered(context, slot))
        {
            return muiRefuse(context);
        }
    }
    bool* showsByCode = &context->focus.showsByCode[player];
    if (cause != mui_focusByCode)
    {
        *showsByCode = cause == mui_focusByNavigation;
    }
    muiFocusAssign(context, player, slot, *showsByCode);
    // A press dismisses popups itself.
    if (cause != mui_focusByPointer)
    {
        muiPopupFocus(context, slot);
    }
    return mui_success;
}

muiNodeId muiFocus_Get(const muiContext* context, uint8_t player)
{
    if (context == nullptr || player >= MUI_MAX_PLAYERS)
    {
        return (muiNodeId){0, 0};
    }
    return context->focus.nodes[player];
}

// The nearest layer root holding a node, itself included, up to root.
static uint32_t LayerOf(const muiTree* tree, uint32_t root, uint32_t slot)
{
    for (uint32_t at = slot; at != root; at = muiTreeAt(tree, at)->links.parent)
    {
        if (muiIsLayerRoot(tree, at))
        {
            return at;
        }
    }
    return root;
}

// The top modal layer under root, or 0.
static uint32_t TopModal(const muiContext* context, uint32_t root)
{
    for (uint32_t i = context->layers.count; i > 0; i--)
    {
        uint32_t layer = muiLayerAt(context, i - 1);
        if (layer != 0 && context->interaction[layer - 1].layer == mui_layerModal &&
            muiTreeIsAncestor(&context->tree, root, layer) &&
            !muiTreeIsExiting(&context->tree, layer))
        {
            return layer;
        }
    }
    return 0;
}

// Where a node comes in sequential order: its tab order, 0 last, then its
// place in tree order.
static uint64_t KeyOf(const muiContext* context, uint32_t slot, uint32_t place)
{
    uint32_t order = context->interaction[slot - 1].tabOrder;
    return (uint64_t)(order == 0 ? 256u : order) << 32 | place;
}

// The nodes around the focus in sequential order: the nearest after and
// before it, and the first and last.
typedef struct Around
{
    // The focus's key; 0 when it is not in the scope, which puts every
    // node after it.
    uint64_t current;
    uint32_t after;
    uint64_t afterKey;
    uint32_t before;
    uint64_t beforeKey;
    uint32_t first;
    uint64_t firstKey;
    uint32_t last;
    uint64_t lastKey;
} Around;

static void Consider(Around* around, uint32_t slot, uint64_t key)
{
    if (around->first == 0 || key < around->firstKey)
    {
        around->first = slot;
        around->firstKey = key;
    }
    if (around->last == 0 || key > around->lastKey)
    {
        around->last = slot;
        around->lastKey = key;
    }
    if (key > around->current && (around->after == 0 || key < around->afterKey))
    {
        around->after = slot;
        around->afterKey = key;
    }
    if (key < around->current && (around->before == 0 || key > around->beforeKey))
    {
        around->before = slot;
        around->beforeKey = key;
    }
}

uint32_t muiFocusFollowing(const muiTree* tree, uint32_t scope, uint32_t at)
{
    // Nothing under an exiting node, nor the scope if it exits; the node
    // itself takes no focus.
    uint32_t child = muiTreeAt(tree, at)->links.firstChild;
    bool skip = child == 0 || (muiTreeAt(tree, at)->flags & MUI_TREE_EXITING) != 0;
    at = skip ? at : child;
    for (;;)
    {
        if (skip)
        {
            while (at != scope && muiTreeAt(tree, at)->links.next == 0)
            {
                at = muiTreeAt(tree, at)->links.parent;
            }
            if (at == scope)
            {
                return 0;
            }
            at = muiTreeAt(tree, at)->links.next;
        }
        if (!muiIsLayerRoot(tree, at))
        {
            return at;
        }
        skip = true;
    }
}

// Walks the scope in tree order: once to the focus for its key, once
// over the nodes sequential navigation reaches.
static Around Survey(const muiContext* context, uint32_t scope, uint32_t focus)
{
    const muiTree* tree = &context->tree;
    Around around = {0};
    uint32_t place = 0;
    for (uint32_t at = focus != 0 ? scope : 0; at != 0;
         at = muiFocusFollowing(tree, scope, at), place++)
    {
        if (at == focus)
        {
            around.current = KeyOf(context, at, place);
            break;
        }
    }
    place = 0;
    for (uint32_t at = scope; at != 0; at = muiFocusFollowing(tree, scope, at), place++)
    {
        // The focus itself is a candidate: alone, it stays.
        if (muiFocusTakes(context, at, mui_focusAll))
        {
            Consider(&around, at, KeyOf(context, at, place));
        }
    }
    return around;
}

uint32_t muiFocusScope(const muiContext* context, uint32_t root, uint8_t player, uint32_t* focusOut)
{
    const muiTree* tree = &context->tree;
    uint32_t focus = SlotOf(context, context->focus.nodes[player]);
    *focusOut = 0;
    if (focus != 0 && muiTreeIsAncestor(tree, root, focus) && !muiFocusIsCovered(context, focus))
    {
        *focusOut = focus;
        return LayerOf(tree, root, focus);
    }
    uint32_t modal = TopModal(context, root);
    return modal != 0 ? modal : root;
}

void muiFocusNavigate(muiContext* context, uint8_t player, uint32_t slot)
{
    context->focus.showsByCode[player] = true;
    muiFocusAssign(context, player, slot, true);
    muiPopupFocus(context, slot);
    muiScrollReveal(context, slot);
}

muiResult muiFocus_Move(muiContext* context, muiNodeId rootId, uint8_t player, bool backward)
{
    if (context == nullptr)
    {
        return mui_errorInvalid;
    }
    if (IsNull(rootId) || player >= MUI_MAX_PLAYERS || muiIsInHostCall(context))
    {
        return muiRefuse(context);
    }
    const muiTree* tree = &context->tree;
    uint32_t root = muiTreeResolve(tree, rootId);
    if (root == 0)
    {
        return mui_errorStale;
    }
    uint32_t focus = 0;
    uint32_t scope = muiFocusScope(context, root, player, &focus);
    const Around around = Survey(context, scope, focus);
    uint32_t next = backward ? (around.before != 0 ? around.before : around.last)
                             : (around.after != 0 ? around.after : around.first);
    if (next == 0)
    {
        return mui_empty;
    }
    muiFocusNavigate(context, player, next);
    return mui_success;
}

void muiFocusPress(muiContext* context, uint8_t player, uint32_t slot)
{
    const muiTree* tree = &context->tree;
    uint32_t at = slot;
    while (at != 0 && !muiFocusTakes(context, at, mui_focusPointer))
    {
        at = muiTreeAt(tree, at)->links.parent;
    }
    if (at != 0 && muiFocusIsCovered(context, at))
    {
        at = 0;
    }
    context->focus.showsByCode[player] = false;
    muiFocusAssign(context, player, at, false);
}

void muiFocusRecheck(muiContext* context, uint32_t slot)
{
    if (muiFocusTakes(context, slot, mui_focusPointer))
    {
        return;
    }
    uint8_t focusedBy = context->style.nodes[slot - 1].focusedBy;
    for (uint32_t player = 0; player < MUI_MAX_PLAYERS; player++)
    {
        if ((focusedBy >> player & 1u) != 0)
        {
            muiFocusAssign(context, (uint8_t)player, 0, false);
        }
    }
}

void muiFocusForgetDestroyed(muiContext* context)
{
    muiFocusStore* store = &context->focus;
    for (uint32_t player = 0; player < MUI_MAX_PLAYERS; player++)
    {
        if (!IsNull(store->nodes[player]) && SlotOf(context, store->nodes[player]) == 0)
        {
            Notify(context, mui_notificationFocusLost, store->nodes[player], (uint8_t)player);
            store->nodes[player] = (muiNodeId){0, 0};
            store->holders &= (uint8_t)~(1u << player);
        }
    }
}
