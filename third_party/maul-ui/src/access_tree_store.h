// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The accessibility tree's consumer (record mui-0008): the nodes a tree
// holds, found by id, and what applying an update stages and retires.

#ifndef MAUL_UI_SRC_ACCESS_TREE_STORE_H
#define MAUL_UI_SRC_ACCESS_TREE_STORE_H

#include "maul-ui/access_tree.h"

#include <stdbool.h>
#include <stdint.h>

// A node held: the record, its texts and links pointing at the tree's
// copies, and its children; an id of 0 marks a free slot.
typedef struct muiHeldNode
{
    muiAccessNode node;
    uint64_t* children;
    // The parent's slot, 0 for none; the apply that last saw a node it
    // sent list this one, and which (its place in the update plus 1); and
    // the apply that found the parents above it end.
    uint32_t parent;
    uint32_t listed;
    uint32_t claimer;
    uint32_t ended;
} muiHeldNode;

// What applying an update copies before it changes anything.
typedef struct muiStagedNode
{
    char* text[MUI_ACCESS_TEXTS];
    muiAccessLink* links;
    uint64_t* children;
} muiStagedNode;

// A node an apply replaced or let go, kept for the report.
typedef struct muiRetiredNode
{
    muiHeldNode held;
    bool removed;
} muiRetiredNode;

// A slot in the update's index for a node new to the tree: the apply it
// was written in, the node sent that lists it (its place in the update
// plus 1, or 0), and the apply that found the parents above it end.
typedef struct muiUpdateSlot
{
    uint64_t id;
    uint32_t apply;
    uint32_t claimer;
    uint32_t ended;
} muiUpdateSlot;

struct muiAccessTree
{
    muiAllocator allocator;
    size_t blockSize;
    uint32_t capacity;
    uint32_t count;
    // Slot i is held[i - 1].
    muiHeldNode* held;
    // Free slots, a stack.
    uint32_t* free;
    uint32_t freeCount;
    // Held slots by id, open addressing with linear probing; 0 is empty.
    uint32_t* map;
    muiUpdateSlot* updates;
    uint32_t mask;
    // The apply under way, counting from 1.
    uint32_t apply;
    muiStagedNode* staged;
    muiRetiredNode* retired;
    uint32_t* added;
    // Scratch for walks: slots, and where each is in its children.
    uint32_t* stack;
    uint32_t* walk;
    uint64_t root;
    uint64_t focus;
};

// The slot holding a node, or 0.
uint32_t muiHeldSlotOf(const muiAccessTree* tree, uint64_t id);

// Frees what a held node owns.
void muiFreeHeld(const muiAccessTree* tree, const muiHeldNode* held);

// Whether a node's new record, held now, may show the tree otherwise than
// its old one did, by what the view's rules read (access_view.c).
bool muiViewDiffers(const muiAccessTree* tree, const muiHeldNode* old, const muiHeldNode* now);

#endif // MAUL_UI_SRC_ACCESS_TREE_STORE_H
