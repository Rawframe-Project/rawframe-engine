// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Exit transitions (record mui-0007): the table of nodes exiting, their
// state and flag, and the reports of exits no transition runs in.

#include "maul-ui/exit.h"

#include "animation.h"
#include "context.h"
#include "exit.h"
#include "exit_store.h"
#include "focus.h"
#include "layout_node.h"
#include "notify.h"
#include "tree.h"

// The index of the entry of the node at slot; the count for none.
static uint32_t IndexOf(const muiContext* context, uint32_t slot)
{
    const muiExitStore* store = &context->exits;
    uint32_t i = 0;
    while (i < store->count && (store->entries[i].node.index1 != slot ||
                                muiTreeResolve(&context->tree, store->entries[i].node) != slot))
    {
        i++;
    }
    return i;
}

// Takes out the entries of nodes destroyed since.
static void Purge(muiContext* context)
{
    muiExitStore* store = &context->exits;
    for (uint32_t i = store->count; i > 0; i--)
    {
        if (muiTreeResolve(&context->tree, store->entries[i - 1].node) == 0)
        {
            store->entries[i - 1] = store->entries[--store->count];
        }
    }
}

// Sets or clears the node's exiting flag and state, restyling it; a node
// popping leaves its parent's flow, or comes back.
static void SetExiting(muiContext* context, uint32_t slot, bool exiting)
{
    muiLayoutNode* layout = &context->layout[slot - 1];
    bool popped = exiting && context->interaction[slot - 1].exitLayout == mui_exitPop;
    if (layout->popped != popped)
    {
        layout->popped = popped;
        muiSyncLayoutNode(layout);
        muiTreeMarkLayout(&context->tree, slot);
    }
    muiTreeNode* node = muiTreeAt(&context->tree, slot);
    muiNodeStyle* style = &context->style.nodes[slot - 1];
    node->flags =
        (uint8_t)(exiting ? node->flags | MUI_TREE_EXITING : node->flags & ~MUI_TREE_EXITING);
    style->states =
        (muiState)(exiting ? style->states | mui_stateExiting : style->states & ~mui_stateExiting);
    style->edited = true;
    muiTreeMark(&context->tree, slot, mui_stageStyle);
}

muiResult muiNode_BeginExit(muiContext* context, muiNodeId nodeId)
{
    if (context == nullptr)
    {
        return mui_errorInvalid;
    }
    muiResult status = mui_success;
    uint32_t slot = muiResolveEdit(context, nodeId, &status);
    if (slot == 0 || (muiTreeAt(&context->tree, slot)->flags & MUI_TREE_EXITING) != 0)
    {
        return status;
    }
    muiExitStore* store = &context->exits;
    if (store->count == store->capacity)
    {
        Purge(context);
    }
    if (store->count == store->capacity)
    {
        return mui_errorCapacity;
    }
    store->entries[store->count++] = (muiExitEntry){muiTreeIdOf(&context->tree, slot), false};
    SetExiting(context, slot, true);
    // Focus inside is given up.
    for (uint32_t player = 0; player < MUI_MAX_PLAYERS; player++)
    {
        uint32_t focus = muiTreeResolve(&context->tree, context->focus.nodes[player]);
        if (focus != 0 && muiTreeIsAncestor(&context->tree, slot, focus))
        {
            muiFocusAssign(context, (uint8_t)player, 0, false);
        }
    }
    return mui_success;
}

muiResult muiNode_CancelExit(muiContext* context, muiNodeId nodeId)
{
    if (context == nullptr)
    {
        return mui_errorInvalid;
    }
    muiResult status = mui_success;
    uint32_t slot = muiResolveEdit(context, nodeId, &status);
    if (slot == 0)
    {
        return status;
    }
    muiExitStore* store = &context->exits;
    uint32_t i = IndexOf(context, slot);
    if (i < store->count)
    {
        store->entries[i] = store->entries[--store->count];
        SetExiting(context, slot, false);
    }
    return mui_success;
}

void muiExitAdvance(muiContext* context, uint32_t root)
{
    muiExitStore* store = &context->exits;
    const muiTree* tree = &context->tree;
    for (uint32_t i = 0; i < store->count; i++)
    {
        muiExitEntry* entry = &store->entries[i];
        uint32_t slot = muiTreeResolve(tree, entry->node);
        // Entries of nodes gone wait for a purge.
        if (slot != 0 && !entry->finished && muiTreeIsAncestor(tree, root, slot) &&
            !muiIsAnimatingUnder(&context->animations, tree, slot))
        {
            entry->finished = true;
            const muiNotification record = {mui_notificationExitFinished, entry->node, 0};
            muiNotifyPost(&context->notifications, &record);
        }
    }
}
