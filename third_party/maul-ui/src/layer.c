// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Layers (record mui-0007). The store holds the nodes that root a layer,
// in paint order from the bottom; entries of nodes destroyed since are
// passed over, and taken out when room is needed.

#include "layer.h"

#include "context.h"
#include "tree.h"

#include <string.h>

// Whether an entry's node still exists: a node that stops being a layer
// is taken out at once, one destroyed leaves its entry behind.
static bool IsLive(const muiContext* context, const muiLayerEntry* entry)
{
    const muiTree* tree = &context->tree;
    muiNodeId id = muiTreeIdOf(tree, entry->slot);
    return id.generation == entry->generation && muiTreeResolve(tree, id) == entry->slot;
}

// The overlay band is above every activation layer.
static uint32_t BandOf(const muiContext* context, uint32_t slot)
{
    return context->interaction[slot - 1].layer == mui_layerOverlay ? 1u : 0u;
}

static uint32_t Find(const muiLayerStore* store, uint32_t slot)
{
    for (uint32_t i = 0; i < store->count; i++)
    {
        if (store->entries[i].slot == slot)
        {
            return i + 1;
        }
    }
    return 0;
}

static void RemoveAt(muiLayerStore* store, uint32_t index)
{
    memmove(&store->entries[index], &store->entries[index + 1],
            (store->count - index - 1) * sizeof(muiLayerEntry));
    store->count--;
}

// Takes out the entries of nodes destroyed, leaving their slots' flags to
// the nodes there now.
static void Purge(muiContext* context)
{
    muiLayerStore* store = &context->layers;
    for (uint32_t i = store->count; i > 0; i--)
    {
        if (!IsLive(context, &store->entries[i - 1]))
        {
            RemoveAt(store, i - 1);
        }
    }
}

// Puts a node in as the latest activated of its band, with its openers;
// nothing when the store is full.
static void Insert(muiContext* context, uint32_t slot, const muiNodeId openers[MUI_MAX_PLAYERS])
{
    muiLayerStore* store = &context->layers;
    if (store->count == store->capacity)
    {
        Purge(context);
    }
    if (store->count == store->capacity)
    {
        return;
    }
    // Last of its band: after every entry of its band or below.
    uint32_t band = BandOf(context, slot);
    uint32_t at = store->count;
    while (at > 0 && BandOf(context, store->entries[at - 1].slot) > band)
    {
        at--;
    }
    memmove(&store->entries[at + 1], &store->entries[at],
            (store->count - at) * sizeof(muiLayerEntry));
    muiLayerEntry* entry = &store->entries[at];
    entry->slot = slot;
    entry->generation = muiTreeIdOf(&context->tree, slot).generation;
    entry->activation = ++store->activations;
    memcpy(entry->openers, openers, sizeof entry->openers);
    store->count++;
    muiTreeAt(&context->tree, slot)->flags |= MUI_TREE_APART;
}

void muiNoteLayer(muiContext* context, uint32_t slot, muiLayerKind before)
{
    muiLayerKind kind = context->interaction[slot - 1].layer;
    if (kind == before)
    {
        return;
    }
    muiLayerStore* store = &context->layers;
    // A layer whose kind changes keeps its openers; a new one, or one in
    // the slot of a layer destroyed, takes what each player focuses
    // outside it.
    muiNodeId openers[MUI_MAX_PLAYERS] = {0};
    uint32_t found = Find(store, slot);
    bool kept = found != 0 && IsLive(context, &store->entries[found - 1]);
    if (kept)
    {
        memcpy(openers, store->entries[found - 1].openers, sizeof openers);
    }
    if (found != 0)
    {
        RemoveAt(store, found - 1);
        muiTreeAt(&context->tree, slot)->flags &= (uint8_t)~MUI_TREE_APART;
    }
    if (!kept)
    {
        for (uint32_t player = 0; player < MUI_MAX_PLAYERS; player++)
        {
            muiNodeId focus = context->focus.nodes[player];
            uint32_t at = focus.index1 != 0 ? muiTreeResolve(&context->tree, focus) : 0;
            openers[player] =
                at != 0 && !muiTreeIsAncestor(&context->tree, slot, at) ? focus : (muiNodeId){0, 0};
        }
    }
    if (kind != mui_layerNone)
    {
        Insert(context, slot, openers);
    }
    muiTreeMark(&context->tree, slot, mui_stagePaint);
}

muiNodeId muiLayerOpener(const muiContext* context, uint32_t slot, uint8_t player)
{
    uint32_t found = Find(&context->layers, slot);
    return found != 0 && IsLive(context, &context->layers.entries[found - 1])
               ? context->layers.entries[found - 1].openers[player]
               : (muiNodeId){0, 0};
}

uint32_t muiLayerAt(const muiContext* context, uint32_t place)
{
    const muiLayerEntry* entry = &context->layers.entries[place];
    return IsLive(context, entry) ? entry->slot : 0;
}

muiResult muiNode_RaiseLayer(muiContext* context, muiNodeId nodeId)
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
    muiLayerStore* store = &context->layers;
    uint32_t found = Find(store, slot);
    if (found == 0)
    {
        return muiRefuse(context);
    }
    // Raised, it keeps its openers.
    muiNodeId openers[MUI_MAX_PLAYERS];
    memcpy(openers, store->entries[found - 1].openers, sizeof openers);
    RemoveAt(store, found - 1);
    Insert(context, slot, openers);
    muiTreeMark(&context->tree, slot, mui_stagePaint);
    return mui_success;
}
