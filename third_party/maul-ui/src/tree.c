// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Slot reuse is last in, first out, so the same edits give the same ids
// on every run.

#include "tree.h"

#include "invariant.h"

void muiTreeInit(muiTree* tree, muiTreeNode* nodes, uint32_t capacity)
{
    *tree = (muiTree){.nodes = nodes, .capacity = capacity};
}

uint32_t muiTreeResolve(const muiTree* tree, muiNodeId nodeId)
{
    if (nodeId.index1 == 0 || nodeId.index1 > tree->used)
    {
        return 0;
    }
    const muiTreeNode* node = &tree->nodes[nodeId.index1 - 1];
    return node->live && node->generation == nodeId.generation ? nodeId.index1 : 0;
}

muiNodeId muiTreeIdOf(const muiTree* tree, uint32_t slot)
{
    if (slot == 0)
    {
        return (muiNodeId){0, 0};
    }
    return (muiNodeId){slot, tree->nodes[slot - 1].generation};
}

uint32_t muiTreeCreate(muiTree* tree, uint64_t hostKey)
{
    uint32_t slot = tree->freeHead;
    uint32_t generation = 1;
    if (slot != 0)
    {
        muiTreeNode* node = muiTreeAt(tree, slot);
        tree->freeHead = node->links.next;
        generation = node->generation;
    }
    else if (tree->used < tree->capacity)
    {
        slot = ++tree->used;
    }
    else
    {
        return 0;
    }
    *muiTreeAt(tree, slot) = (muiTreeNode){
        .hostKey = hostKey,
        .generation = generation,
        .dirty = {.request = mui_stageAll, .subtree = mui_stageAll},
        .live = true,
    };
    tree->liveCount++;
    return slot;
}

bool muiTreeIsAncestor(const muiTree* tree, uint32_t ancestor, uint32_t node)
{
    for (uint32_t at = node; at != 0; at = muiTreeAt(tree, at)->links.parent)
    {
        if (at == ancestor)
        {
            return true;
        }
    }
    return false;
}

// Sets subtree bits from node up to the first node that has them all.
static void MarkSubtree(muiTree* tree, uint32_t node, muiStages stages)
{
    for (uint32_t at = node; at != 0;)
    {
        muiTreeNode* entry = muiTreeAt(tree, at);
        if ((entry->dirty.subtree & stages) == stages)
        {
            return;
        }
        entry->dirty.subtree |= stages;
        at = entry->links.parent;
    }
}

void muiTreeMark(muiTree* tree, uint32_t node, muiStages stages)
{
    muiTreeAt(tree, node)->dirty.request |= stages;
    MarkSubtree(tree, node, stages);
}

void muiTreeMarkAll(muiTree* tree, muiStages stages)
{
    for (uint32_t slot = 1; slot <= tree->used; slot++)
    {
        if (muiTreeAt(tree, slot)->live)
        {
            muiTreeMark(tree, slot, stages);
        }
    }
}

void muiTreeMarkLayout(muiTree* tree, uint32_t node)
{
    muiTreeMark(tree, node, mui_stageLayout | mui_stagePaint);
    uint32_t parent = muiTreeAt(tree, node)->links.parent;
    if (parent != 0)
    {
        muiTreeMark(tree, parent, mui_stageLayout | mui_stagePaint);
    }
}

void muiTreeInsert(muiTree* tree, uint32_t parent, uint32_t child, uint32_t before)
{
    muiTreeNode* parentNode = muiTreeAt(tree, parent);
    muiTreeNode* childNode = muiTreeAt(tree, child);
    MUI_ASSERT(childNode->links.parent == 0 && !muiTreeIsAncestor(tree, child, parent));
    uint32_t previous =
        before != 0 ? muiTreeAt(tree, before)->links.previous : parentNode->links.lastChild;
    childNode->links.parent = parent;
    childNode->links.previous = previous;
    childNode->links.next = before;
    if (previous != 0)
    {
        muiTreeAt(tree, previous)->links.next = child;
    }
    else
    {
        parentNode->links.firstChild = child;
    }
    if (before != 0)
    {
        muiTreeAt(tree, before)->links.previous = child;
    }
    else
    {
        parentNode->links.lastChild = child;
    }
    parentNode->links.childCount++;
    // What the child's subtree still owes now lies below the parent too.
    MarkSubtree(tree, parent, childNode->dirty.subtree);
    muiTreeMark(tree, child, mui_stageStyle);
    muiTreeMark(tree, parent, mui_stageLayout | mui_stagePaint);
}

void muiTreeDetach(muiTree* tree, uint32_t node)
{
    muiTreeNode* entry = muiTreeAt(tree, node);
    uint32_t parent = entry->links.parent;
    if (parent == 0)
    {
        return;
    }
    muiTreeNode* parentNode = muiTreeAt(tree, parent);
    if (entry->links.previous != 0)
    {
        muiTreeAt(tree, entry->links.previous)->links.next = entry->links.next;
    }
    else
    {
        parentNode->links.firstChild = entry->links.next;
    }
    if (entry->links.next != 0)
    {
        muiTreeAt(tree, entry->links.next)->links.previous = entry->links.previous;
    }
    else
    {
        parentNode->links.lastChild = entry->links.previous;
    }
    parentNode->links.childCount--;
    entry->links.parent = 0;
    entry->links.next = 0;
    entry->links.previous = 0;
    // What it reads from above, its themes, changes.
    muiTreeMark(tree, node, mui_stageStyle);
    muiTreeMark(tree, parent, mui_stageLayout | mui_stagePaint);
}

// Frees a node that has no children and is its parent's first child, or
// a root.
static void FreeLeaf(muiTree* tree, uint32_t slot)
{
    muiTreeNode* node = muiTreeAt(tree, slot);
    MUI_ASSERT(node->links.firstChild == 0 && node->links.previous == 0);
    if (node->links.parent != 0)
    {
        muiTreeNode* parent = muiTreeAt(tree, node->links.parent);
        parent->links.firstChild = node->links.next;
        if (node->links.next != 0)
        {
            muiTreeAt(tree, node->links.next)->links.previous = 0;
        }
        else
        {
            parent->links.lastChild = 0;
        }
        parent->links.childCount--;
    }
    uint32_t generation = node->generation + 1;
    *node = (muiTreeNode){
        .links = {.next = tree->freeHead},
        .generation = generation != 0 ? generation : 1,
    };
    tree->freeHead = slot;
    tree->liveCount--;
}

void muiTreeDestroy(muiTree* tree, uint32_t node)
{
    muiTreeDetach(tree, node);
    uint32_t at = node;
    for (;;)
    {
        while (muiTreeAt(tree, at)->links.firstChild != 0)
        {
            at = muiTreeAt(tree, at)->links.firstChild;
        }
        const muiTreeLinks links = muiTreeAt(tree, at)->links;
        bool last = at == node;
        FreeLeaf(tree, at);
        if (last)
        {
            return;
        }
        at = links.next != 0 ? links.next : links.parent;
    }
}

// The first of node and its following siblings with one of stages owed
// in its subtree, or 0.
static uint32_t NextOwing(const muiTree* tree, uint32_t node, muiStages stages)
{
    uint32_t at = node;
    while (at != 0 && (muiTreeAt(tree, at)->dirty.subtree & stages) == 0)
    {
        at = muiTreeAt(tree, at)->links.next;
    }
    return at;
}

uint32_t muiTreeNextOwing(const muiTree* tree, uint32_t root, uint32_t at, muiStages stages)
{
    if (at == 0)
    {
        return (muiTreeAt(tree, root)->dirty.subtree & stages) != 0 ? root : 0;
    }
    uint32_t child = NextOwing(tree, muiTreeAt(tree, at)->links.firstChild, stages);
    if (child != 0)
    {
        return child;
    }
    for (uint32_t node = at; node != root; node = muiTreeAt(tree, node)->links.parent)
    {
        uint32_t sibling = NextOwing(tree, muiTreeAt(tree, node)->links.next, stages);
        if (sibling != 0)
        {
            return sibling;
        }
    }
    return 0;
}

uint32_t muiTreeSweep(muiTree* tree, uint32_t root, muiStages stages)
{
    uint32_t reached = 0;
    for (uint32_t at = muiTreeNextOwing(tree, root, 0, stages); at != 0;
         at = muiTreeNextOwing(tree, root, at, stages))
    {
        muiTreeNode* node = muiTreeAt(tree, at);
        node->dirty.request &= (muiStages)~stages;
        node->dirty.subtree &= (muiStages)~stages;
        reached++;
    }
    return reached;
}
