// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Building draw lists (record mui-0005). The walk is preorder over the
// tree's links, each node reading what its parent left in its paint
// state, so it needs no stack. A list is built from the last one: a
// subtree no paint request reaches, at the origin and opacity it was
// painted at, copies its commands, clips, gradients and glyphs and
// renumbers them; a build with nothing to repaint keeps the last list as it is.

#include "maul-ui/draw.h"

#include "context.h"
#include "draw_store.h"
#include "paint.h"
#include "paint_host.h"
#include "tree.h"

#include <math.h>
#include <string.h>

// One build: the painter writing the new tables, the last list to take
// from (NULL when none may be taken), and the builds' numbers.
typedef struct Build
{
    muiPainter painter;
    muiDrawStore* store;
    const muiDrawTables* previous;
    uint64_t previousBuild;
    uint64_t build;
} Build;

// Where a copied subtree's indices land: its own clips and gradients
// move by the difference of their first entries, and the clip it was
// painted in becomes the one it is painted in now.
typedef struct Renumbering
{
    muiDrawRange clips;
    uint32_t clipBase;
    uint32_t entryClip;
    muiDrawRange gradients;
    uint32_t gradientBase;
} Renumbering;

static uint32_t ClipOf(const Renumbering* renumbering, uint32_t clip)
{
    const muiDrawRange* own = &renumbering->clips;
    return clip >= own->first && clip < own->end ? clip - own->first + renumbering->clipBase
                                                 : renumbering->entryClip;
}

// Whether a node's subtree can be taken from the last list.
static bool CanCopy(const Build* build, uint32_t slot, const muiPaintState* old,
                    const muiPaintState* now)
{
    const muiTree* tree = &build->painter.context->tree;
    return build->previous != nullptr && old->build == build->previousBuild &&
           (muiTreeAt(tree, slot)->dirty.subtree & mui_stagePaint) == 0 && old->x == now->x &&
           old->y == now->y && old->inherited == now->inherited;
}

// Copies a subtree's commands, clips, gradients and glyphs; false when
// its glyphs do not fit. A subtree's glyph runs name one contiguous span
// of glyphs, in order, so the span is found from them as they are copied.
static bool CopyCommands(Build* build, const muiPaintState* old, const Renumbering* renumbering)
{
    const muiDrawTables* from = build->previous;
    muiDrawTables* to = build->painter.out;
    uint32_t glyphFirst = 0;
    uint32_t glyphEnd = 0;
    bool glyphs = false;
    for (uint32_t i = old->commands.first; i < old->commands.end; i++)
    {
        muiDrawCommand* command = &to->commands[to->commandCount++];
        memcpy(command, &from->commands[i], sizeof *command);
        command->clip = ClipOf(renumbering, command->clip);
        if (command->kind == mui_drawBox && command->box.gradient != 0)
        {
            command->box.gradient =
                command->box.gradient - renumbering->gradients.first + renumbering->gradientBase;
        }
        if (command->kind == mui_drawGlyphRun)
        {
            muiDrawGlyphRun* run = &command->glyphRun;
            if (!glyphs)
            {
                glyphFirst = run->firstGlyph;
                glyphs = true;
            }
            glyphEnd = run->firstGlyph + run->glyphCount;
            run->firstGlyph = run->firstGlyph - glyphFirst + to->glyphCount;
        }
    }
    for (uint32_t i = old->clips.first; i < old->clips.end; i++)
    {
        muiDrawClip* clip = &to->clips[to->clipCount++];
        memcpy(clip, &from->clips[i], sizeof *clip);
        clip->parent = ClipOf(renumbering, clip->parent);
    }
    uint32_t gradients = old->gradients.end - old->gradients.first;
    memcpy(&to->gradients[to->gradientCount], &from->gradients[old->gradients.first],
           gradients * sizeof(muiDrawGradient));
    to->gradientCount += gradients;
    uint32_t glyphCount = glyphEnd - glyphFirst;
    if (glyphCount > build->painter.glyphCapacity - to->glyphCount)
    {
        build->painter.full = true;
        return false;
    }
    memcpy(&to->glyphs[to->glyphCount], &from->glyphs[glyphFirst], glyphCount * sizeof(muiGlyph));
    to->glyphCount += glyphCount;
    return true;
}

// Moves a span painted by the last build to where a copy put it.
static void Shift(muiDrawRange* range, uint32_t from, uint32_t to)
{
    range->first = range->first - from + to;
    range->end = range->end - from + to;
}

// Brings the paint states of a copied node's descendants up to this
// build: their spans move with the copy.
static void Renumber(Build* build, uint32_t node, const muiPaintState* old,
                     const muiPaintState* now)
{
    const muiTree* tree = &build->painter.context->tree;
    muiPaintState* states = build->store->states;
    for (uint32_t at = muiTreeAt(tree, node)->links.firstChild; at != 0;)
    {
        muiPaintState* state = &states[at - 1];
        if (state->build == build->previousBuild)
        {
            state->build = build->build;
            Shift(&state->commands, old->commands.first, now->commands.first);
            Shift(&state->clips, old->clips.first, now->clips.first);
            Shift(&state->gradients, old->gradients.first, now->gradients.first);
        }
        // The next node in preorder below node.
        if (muiTreeAt(tree, at)->links.firstChild != 0)
        {
            at = muiTreeAt(tree, at)->links.firstChild;
            continue;
        }
        while (at != node && muiTreeAt(tree, at)->links.next == 0)
        {
            at = muiTreeAt(tree, at)->links.parent;
        }
        at = at == node ? 0 : muiTreeAt(tree, at)->links.next;
    }
}

// Takes a node's subtree, painted in entryClip now, from the last list
// into state; false when it does not fit.
static bool Copy(Build* build, uint32_t node, muiPaintState* state, uint32_t entryClip)
{
    const muiPaintState old = *state;
    muiDrawTables* to = build->painter.out;
    muiPainter* painter = &build->painter;
    if (old.commands.end - old.commands.first > painter->commandCapacity - to->commandCount ||
        old.clips.end - old.clips.first > painter->clipCapacity - to->clipCount ||
        old.gradients.end - old.gradients.first > painter->gradientCapacity - to->gradientCount)
    {
        painter->full = true;
        return false;
    }
    const Renumbering renumbering = {old.clips, to->clipCount, entryClip, old.gradients,
                                     to->gradientCount};
    state->commands = (muiDrawRange){to->commandCount, 0};
    state->clips = (muiDrawRange){to->clipCount, 0};
    state->gradients = (muiDrawRange){to->gradientCount, 0};
    if (!CopyCommands(build, &old, &renumbering))
    {
        return false;
    }
    state->commands.end = to->commandCount;
    state->clips.end = to->clipCount;
    state->gradients.end = to->gradientCount;
    state->build = build->build;
    Renumber(build, node, &old, state);
    return true;
}

// Paints or copies a node from its parent's state; true when its
// children are to be visited.
static bool Visit(Build* build, uint32_t root, uint32_t at)
{
    const muiContext* context = build->painter.context;
    const muiTree* tree = &context->tree;
    const muiPaintState top = {.opacity = 1.0f};
    uint32_t parent = at == root ? 0 : muiTreeAt(tree, at)->links.parent;
    const muiPaintState* above = parent != 0 ? &build->store->states[parent - 1] : &top;
    const muiRect* rect = &context->layout[at - 1].rect;
    muiPaintState* state = &build->store->states[at - 1];
    const muiPaintState now = {
        .build = build->build,
        .rect = *rect,
        .x = above->x + rect->x,
        .y = above->y + rect->y,
        .clip = above->clip,
        .inherited = above->opacity,
    };
    if (CanCopy(build, at, state, &now))
    {
        (void)Copy(build, at, state, now.clip);
        return false;
    }
    const muiDrawTables* out = build->painter.out;
    *state = now;
    state->commands.first = out->commandCount;
    state->clips.first = out->clipCount;
    state->gradients.first = out->gradientCount;
    if (!muiPaintNode(&build->painter, at, state))
    {
        return false;
    }
    if (build->painter.paint != nullptr)
    {
        muiPaintHostContent(&build->painter, at, state);
    }
    return true;
}

// Ends a node's spans where its subtree ended.
static void Leave(Build* build, uint32_t at)
{
    muiPaintState* state = &build->store->states[at - 1];
    const muiDrawTables* out = build->painter.out;
    state->commands.end = out->commandCount;
    state->clips.end = out->clipCount;
    state->gradients.end = out->gradientCount;
}

static void Walk(Build* build, uint32_t root)
{
    const muiTree* tree = &build->painter.context->tree;
    for (uint32_t at = root; at != 0 && !build->painter.full;)
    {
        if (Visit(build, root, at) && muiTreeAt(tree, at)->links.firstChild != 0)
        {
            at = muiTreeAt(tree, at)->links.firstChild;
            continue;
        }
        for (;;)
        {
            Leave(build, at);
            if (at == root)
            {
                at = 0;
                break;
            }
            if (muiTreeAt(tree, at)->links.next != 0)
            {
                at = muiTreeAt(tree, at)->links.next;
                break;
            }
            at = muiTreeAt(tree, at)->links.parent;
        }
    }
}

static void Empty(muiDrawTables* tables)
{
    tables->commandCount = 0;
    tables->clipCount = 1;
    tables->gradientCount = 1;
    tables->glyphCount = 0;
}

// Whether the last list stands for this build: of this root, surface and
// scale, with no paint requested below the root. A failed build left a
// header of scale 0, which no build has. A new node in the root's slot
// is requested every stage until a build of it, which records it.
static bool IsUnchanged(const muiDrawStore* store, const muiTree* tree, uint32_t root,
                        const muiDrawInput* input)
{
    return store->rootIndex == root && store->header.surface == input->surface &&
           store->header.scale == input->scale &&
           (muiTreeAt(tree, root)->dirty.subtree & mui_stagePaint) == 0;
}

muiResult muiBuildDrawList(muiContext* context, muiNodeId rootId, const muiDrawInput* input)
{
    if (context == nullptr)
    {
        return mui_errorInvalid;
    }
    if (input == nullptr || rootId.index1 == 0 || !isfinite(input->scale) || input->scale <= 0.0f ||
        muiIsInHostCall(context))
    {
        return muiRefuse(context);
    }
    uint32_t root = muiTreeResolve(&context->tree, rootId);
    if (root == 0)
    {
        return mui_errorStale;
    }
    muiDrawStore* store = &context->draw;
    if (IsUnchanged(store, &context->tree, root, input))
    {
        return mui_success;
    }
    // Spans are taken only at the origin and opacity they were painted
    // at, so the last list may be of another root; a list of scale 0 is
    // none.
    bool takes = store->header.scale == input->scale;
    uint32_t next = 1 - store->current;
    // Large for the stack frame of a public call, so it is cleared once.
    static_assert(sizeof(Build) < 8192, "a build fits a stack frame");
    Build build;
    memset(&build, 0, sizeof build);
    build.painter.context = context;
    build.painter.out = &store->tables[next];
    build.painter.commandCapacity = store->commandCapacity;
    build.painter.clipCapacity = store->clipCapacity;
    build.painter.gradientCapacity = store->gradientCapacity;
    build.painter.glyphCapacity = store->glyphCapacity;
    build.painter.paint = input->paint;
    build.painter.paintUser = input->paintUser;
    build.painter.scale = input->scale;
    build.store = store;
    build.previous = takes ? &store->tables[store->current] : nullptr;
    build.previousBuild = store->header.generation;
    build.build = store->header.generation + 1;
    Empty(build.painter.out);
    // The host's paint function may read the context, not edit it.
    context->inHostCall = true;
    Walk(&build, root);
    context->inHostCall = false;
    context->misuse += build.painter.misuse;
    store->current = next;
    if (build.painter.full)
    {
        Empty(build.painter.out);
        store->header = (muiDrawHeader){.generation = build.build};
        return mui_errorCapacity;
    }
    const muiRect* rect = &context->layout[root - 1].rect;
    store->header = (muiDrawHeader){
        .surface = input->surface,
        .generation = build.build,
        .width = rect->width,
        .height = rect->height,
        .scale = input->scale,
    };
    store->rootIndex = root;
    (void)muiTreeSweep(&context->tree, root, mui_stagePaint);
    return mui_success;
}

muiResult muiGetDrawList(const muiContext* context, muiDrawList* listOut)
{
    if (context == nullptr || listOut == nullptr)
    {
        return mui_errorInvalid;
    }
    const muiDrawStore* store = &context->draw;
    const muiDrawTables* tables = &store->tables[store->current];
    *listOut = (muiDrawList){
        .header = store->header,
        .commands = tables->commands,
        .commandCount = tables->commandCount,
        .clipCount = tables->clipCount,
        .clips = tables->clips,
        .transforms = &store->identity,
        .transformCount = 1,
        .gradientCount = tables->gradientCount,
        .gradients = tables->gradients,
        .glyphs = tables->glyphs,
        .glyphCount = tables->glyphCount,
    };
    return mui_success;
}
