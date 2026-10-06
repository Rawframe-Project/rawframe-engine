// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Accessibility (record mui-0008): the host's data per node, the roots
// updates are built for, and while any is, what was last sent of each
// node.

#ifndef MAUL_UI_SRC_ACCESS_STORE_H
#define MAUL_UI_SRC_ACCESS_STORE_H

#include "allocator.h"

#include "maul-ui/access.h"

#include <stdalign.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// A node's data from the host: an entry per node that has any.
typedef struct muiAccessEntry
{
    muiNodeId node;
    muiRole role;
    muiAccessFlags flags;
    // Counts every edit, so a build tells an edited node from one sent.
    uint32_t version;
    // Allocated with their NUL; NULL for none.
    char* text[MUI_ACCESS_TEXTS];
    uint32_t length[MUI_ACCESS_TEXTS];
    muiAccessValues values;
    // Allocated, in order of kind; NULL for none.
    muiAccessLink* links;
    uint32_t linkCount;
} muiAccessEntry;

// A root updates are built for, whether its next update is whole, and a
// fingerprint of its layers when the last was built: which nodes can
// take focus depends on them.
typedef struct muiAccessRoot
{
    muiNodeId node;
    bool whole;
    uint64_t layers;
} muiAccessRoot;

// What was last sent of the node at a slot, and of its place among its
// parent's children; allocated while a root is enabled.
typedef struct muiAccessSent
{
    // The parent that listed it, its place there, its generation then,
    // and which of the parent's lists it was in: a node its parent lists
    // anew was out of the tree the adapters keep, with its subtree.
    uint32_t parent;
    uint32_t place;
    uint32_t generation;
    uint32_t list;
    // How many children it listed, which list that was (counting each
    // list sent), its host data's version, and a fingerprint of the text
    // the text function gave it.
    uint32_t childCount;
    uint32_t serial;
    uint32_t version;
    uint64_t content;
} muiAccessSent;

typedef struct muiAccessStore
{
    muiAccessEntry* entries;
    uint32_t count;
    uint32_t capacity;
    // Per slot: the index of its entry plus 1, 0 for none; an entry of a
    // node since destroyed stays until its slot or room is needed.
    uint32_t* entryOf;
    muiAccessRoot* roots;
    uint32_t rootCount;
    uint32_t rootCapacity;
    // Allocated by the first root enabled, for every slot: the node as
    // last sent (an id of 0 for none), and its place; the update's nodes
    // and children.
    muiAccessNode* copies;
    muiAccessSent* sent;
    const muiAccessNode** nodes;
    uint64_t* children;
    // A scratch list of slots: children being ordered.
    uint32_t* order;
    // The one block the five lie in, for slots slots.
    void* buffers;
    uint32_t slots;
    // Where host content's text comes from.
    muiAccessTextFunction textFunction;
    void* textUser;
} muiAccessStore;

// The bytes the buffers take for each slot.
static inline size_t muiAccessSlotBytes(void)
{
    return sizeof(muiAccessNode) + sizeof(muiAccessSent) + sizeof(const muiAccessNode*) +
           sizeof(uint64_t) + sizeof(uint32_t);
}

// Frees the buffers enabling allocated.
static inline void muiAccessFreeBuffers(muiAccessStore* store, const muiAllocator* allocator)
{
    if (store->buffers != nullptr)
    {
        muiRelease(allocator, store->buffers, (size_t)store->slots * muiAccessSlotBytes(),
                   alignof(max_align_t));
    }
    store->buffers = nullptr;
    store->copies = nullptr;
    store->sent = nullptr;
    store->nodes = nullptr;
    store->children = nullptr;
    store->order = nullptr;
}

// Frees one of an entry's texts.
static inline void muiAccessFreeText(muiAccessEntry* entry, const muiAllocator* allocator,
                                     uint32_t kind)
{
    if (entry->text[kind] != nullptr)
    {
        muiRelease(allocator, entry->text[kind], (size_t)entry->length[kind] + 1, 1);
    }
    entry->text[kind] = nullptr;
    entry->length[kind] = 0;
}

// Frees an entry's links.
static inline void muiAccessFreeLinks(muiAccessEntry* entry, const muiAllocator* allocator)
{
    if (entry->links != nullptr)
    {
        muiRelease(allocator, entry->links, (size_t)entry->linkCount * sizeof(muiAccessLink),
                   alignof(muiAccessLink));
    }
    entry->links = nullptr;
    entry->linkCount = 0;
}

// Frees what an entry allocated: its texts and links.
static inline void muiAccessFreeEntry(muiAccessEntry* entry, const muiAllocator* allocator)
{
    for (uint32_t kind = 0; kind < MUI_ACCESS_TEXTS; kind++)
    {
        muiAccessFreeText(entry, allocator, kind);
    }
    muiAccessFreeLinks(entry, allocator);
}

// Frees everything the store allocated: every entry's and the buffers.
static inline void muiAccessRelease(muiAccessStore* store, const muiAllocator* allocator)
{
    for (uint32_t i = 0; i < store->count; i++)
    {
        muiAccessFreeEntry(&store->entries[i], allocator);
    }
    muiAccessFreeBuffers(store, allocator);
}

#endif // MAUL_UI_SRC_ACCESS_STORE_H
