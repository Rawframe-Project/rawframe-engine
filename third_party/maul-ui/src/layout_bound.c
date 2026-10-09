// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Layout bounded by the change (record mui-0003). A parent reads of a
// child its answers to the sizing queries it asked, each kept in the
// child's cache, and its baseline where one is aligned by. So a node
// layout was requested on is solved again under the queries its cache
// held; when every answer holds and no baseline is read through it, it
// is laid out alone at its last size and its parent is left as it is.
// When one does not, its parent is cleared and checked the same way,
// up to the root, which the whole solve then takes.

#include "layout_bound.h"

#include "tree.h"

enum
{
    // The most nodes checked at once; a wider change solves the whole
    // tree.
    MAX_CHECKED = 32
};

// A node whose cache was cleared, with what it held, and whether it was
// checked and found to hold.
typedef struct Checked
{
    uint32_t node;
    uint32_t depth;
    muiLayoutCache old;
    bool done;
    bool holds;
    bool laid;
} Checked;

typedef struct Bound
{
    const muiSolver* solver;
    uint32_t root;
    Checked checked[MAX_CHECKED];
    uint32_t count;
} Bound;

static uint32_t DepthOf(const muiTree* tree, uint32_t node)
{
    uint32_t depth = 0;
    for (uint32_t at = muiTreeAt(tree, node)->links.parent; at != 0;
         at = muiTreeAt(tree, at)->links.parent)
    {
        depth++;
    }
    return depth;
}

// Clears a node's cache, keeping what it held; false when too many are.
static bool Clear(Bound* bound, uint32_t node)
{
    if (bound->count == MAX_CHECKED)
    {
        return false;
    }
    muiLayoutCache* cache = &bound->solver->nodes[node - 1].cache;
    bound->checked[bound->count++] =
        (Checked){node, DepthOf(bound->solver->tree, node), *cache, false, false, false};
    *cache = (muiLayoutCache){0};
    return true;
}

static bool IsCleared(const Bound* bound, uint32_t node)
{
    for (uint32_t i = 0; i < bound->count; i++)
    {
        if (bound->checked[i].node == node)
        {
            return true;
        }
    }
    return false;
}

// Whether a node's baseline may be read: aligned by its parent, or the
// parent's, which follows its first child's, read above it.
static bool BaselineRead(const muiSolver* solver, uint32_t node)
{
    for (uint32_t at = node; muiTreeAt(solver->tree, at)->links.parent != 0;
         at = muiTreeAt(solver->tree, at)->links.parent)
    {
        uint32_t parent = muiTreeAt(solver->tree, at)->links.parent;
        if (solver->nodes[parent - 1].style.container.alignItems == mui_alignBaseline ||
            solver->nodes[at - 1].style.item.alignSelf == mui_alignBaseline)
        {
            return true;
        }
    }
    return false;
}

// Whether a cleared node answers every query its cache held as it did.
static bool Holds(const muiSolver* solver, const Checked* checked)
{
    const muiLayoutNode* layout = &solver->nodes[checked->node - 1];
    if (checked->old.replaced || !checked->old.finalValid || layout->listed || layout->popped ||
        BaselineRead(solver, checked->node))
    {
        return false;
    }
    for (int i = 0; i < MUI_CACHE_ENTRIES; i++)
    {
        const muiCacheEntry* entry = &checked->old.entries[i];
        if (!entry->valid)
        {
            continue;
        }
        muiSize size = solver->solve(solver, checked->node, &entry->input, false);
        if (size.width != entry->size.width || size.height != entry->size.height)
        {
            return false;
        }
    }
    // What its parent's cache knew of sizes resolved below it, if read.
    return checked->old.scaledBelow == 0 ||
           (checked->old.scaledBelow == 2) ==
               muiScaledBelow(solver->tree, solver->nodes, checked->node);
}

// The deepest node not yet checked, or none.
static Checked* Deepest(Bound* bound)
{
    Checked* deepest = nullptr;
    for (uint32_t i = 0; i < bound->count; i++)
    {
        Checked* c = &bound->checked[i];
        if (!c->done && (deepest == nullptr || c->depth > deepest->depth))
        {
            deepest = c;
        }
    }
    return deepest;
}

// The shallowest node that holds and is not yet laid out, or none.
static Checked* Shallowest(Bound* bound)
{
    Checked* shallowest = nullptr;
    for (uint32_t i = 0; i < bound->count; i++)
    {
        Checked* c = &bound->checked[i];
        if (c->holds && !c->laid && (shallowest == nullptr || c->depth < shallowest->depth))
        {
            shallowest = c;
        }
    }
    return shallowest;
}

// Lays a node that holds out alone, at the input it last had. Each node
// between it and a change below was cleared on the way up, so its
// layout reaches them; one a change below did not reach that far was
// laid out alone itself.
static void LayOutAlone(const muiSolver* solver, const Checked* checked)
{
    // Exact on both axes, a node reads no extent of its parent's: limits
    // and an aspect ratio bear only on a size not given.
    const muiSizingInput input = {
        .width = {checked->old.finalSize.width, mui_measureExact},
        .height = {checked->old.finalSize.height, mui_measureExact},
        .rtl = checked->old.finalRtl,
        .contentHeight = checked->old.finalContentHeight,
    };
    (void)solver->solve(solver, checked->node, &input, true);
}

bool muiBoundLayout(const muiSolver* solver, uint32_t root)
{
    // Large for the stack frame of a public call, so it is cleared once.
    static_assert(sizeof(Bound) < 8192, "a bound fits a stack frame");
    Bound bound;
    bound.solver = solver;
    bound.root = root;
    bound.count = 0;
    const muiTree* tree = solver->tree;
    for (uint32_t at = muiTreeNextOwing(tree, root, 0, mui_stageLayout); at != 0;
         at = muiTreeNextOwing(tree, root, at, mui_stageLayout))
    {
        if ((muiTreeAt(tree, at)->dirty.request & mui_stageLayout) != 0 &&
            (at == root || !Clear(&bound, at)))
        {
            return false;
        }
    }
    for (Checked* c = Deepest(&bound); c != nullptr; c = Deepest(&bound))
    {
        c->done = true;
        c->holds = Holds(solver, c);
        uint32_t parent = muiTreeAt(tree, c->node)->links.parent;
        if (!c->holds && !IsCleared(&bound, parent) && (parent == root || !Clear(&bound, parent)))
        {
            return false;
        }
    }
    // Those inside a cleared parent are laid out with it; the rest alone,
    // outermost first. An outer one's layout may reach an inner one, as
    // when its direction changed, and lays it out at the input it now
    // has: one so laid out already is left as it is.
    for (Checked* c = Shallowest(&bound); c != nullptr; c = Shallowest(&bound))
    {
        c->laid = true;
        if (!IsCleared(&bound, muiTreeAt(tree, c->node)->links.parent) &&
            !solver->nodes[c->node - 1].cache.finalValid)
        {
            LayOutAlone(solver, c);
        }
    }
    return true;
}
