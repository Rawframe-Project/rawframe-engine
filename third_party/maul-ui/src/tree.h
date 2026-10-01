// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The node store: one reserved slot array, children as intrusive sibling
// links, and two dirty flags per pipeline stage (record mui-0002). Slots
// are 1-based; 0 means none. Callers resolve ids and check edits before
// they reach these functions.

#ifndef MAUL_UI_SRC_TREE_H
#define MAUL_UI_SRC_TREE_H

#include "invariant.h"

#include "maul-ui/base.h"

#include <stdbool.h>

// The pipeline stages a dirty flag names, as bits.
typedef uint8_t muiStages;

enum
{
    mui_stageStyle = 1,
    mui_stageLayout = 2,
    mui_stagePaint = 4,
    mui_stageAll = 7,
};

// Where a node sits: its parent, children and siblings, as slots.
typedef struct muiTreeLinks
{
    uint32_t parent;
    uint32_t firstChild;
    uint32_t lastChild;
    // The next sibling; for a free slot, the next free slot.
    uint32_t next;
    uint32_t previous;
    uint32_t childCount;
} muiTreeLinks;

// What must be redone: request for the node itself, subtree for the
// node or any descendant. A request always has its subtree bit.
typedef struct muiTreeDirty
{
    muiStages request;
    muiStages subtree;
} muiTreeDirty;

typedef struct muiTreeNode
{
    muiTreeLinks links;
    uint64_t hostKey;
    uint32_t generation;
    muiTreeDirty dirty;
    bool live;
} muiTreeNode;

typedef struct muiTree
{
    // Slot i is nodes[i - 1].
    muiTreeNode* nodes;
    uint32_t capacity;
    // Slots ever handed out; those above are untouched.
    uint32_t used;
    // The most recently freed slot, reused first.
    uint32_t freeHead;
    uint32_t liveCount;
} muiTree;

// Sets up a tree over nodes, an array of capacity zeroed slots.
void muiTreeInit(muiTree* tree, muiTreeNode* nodes, uint32_t capacity);

// The slot an id names while its node lives, or 0.
uint32_t muiTreeResolve(const muiTree* tree, muiNodeId nodeId);

// The id of a live slot; the null id for slot 0.
muiNodeId muiTreeIdOf(const muiTree* tree, uint32_t slot);

// A live slot's node. Inline: every walk over the tree calls it.
static inline muiTreeNode* muiTreeAt(const muiTree* tree, uint32_t slot)
{
    MUI_ASSERT(slot != 0 && slot <= tree->used);
    return &tree->nodes[slot - 1];
}

// Makes a root and returns its slot, or 0 when every slot is in use. Its
// every stage is requested.
uint32_t muiTreeCreate(muiTree* tree, uint64_t hostKey);

// Whether ancestor is node or one of its ancestors.
bool muiTreeIsAncestor(const muiTree* tree, uint32_t ancestor, uint32_t node);

// Links a root child into parent before one of parent's children, or
// last for 0, and marks what the edit affects.
void muiTreeInsert(muiTree* tree, uint32_t parent, uint32_t child, uint32_t before);

// Makes node a root, marking what the edit affects. A root is left as
// it is.
void muiTreeDetach(muiTree* tree, uint32_t node);

// Detaches node and frees it with its subtree, children first, in order.
void muiTreeDestroy(muiTree* tree, uint32_t node);

// Requests stages on node and marks its ancestors' subtree flags, up to
// the first that has them already.
void muiTreeMark(muiTree* tree, uint32_t node, muiStages stages);

// Requests stages on every live node.
void muiTreeMarkAll(muiTree* tree, muiStages stages);

// Requests layout and paint on node and its parent: a change to a node's
// size changes its parent's layout too.
void muiTreeMarkLayout(muiTree* tree, uint32_t node);

// The node after at, in preorder from root, among the nodes whose subtree
// flags hold one of stages, descending only into those; at 0 starts at
// root. Returns 0 at the end. Clearing at's flags before the call does not
// change the walk.
uint32_t muiTreeNextOwing(const muiTree* tree, uint32_t root, uint32_t at, muiStages stages);

// The walk every pass makes: visits what muiTreeNextOwing visits, clears
// those stages' flags on each, and returns how many nodes it reached.
uint32_t muiTreeSweep(muiTree* tree, uint32_t root, muiStages stages);

#endif // MAUL_UI_SRC_TREE_H
