// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Building draw lists (records mui-0005, mui-0007). The walk is preorder
// over the tree's links, each node reading what its parent left in its
// paint state, so it needs no stack; layers are walked after, from the
// bottom. A list is built from the last one: a subtree no paint request
// reaches, at the origin and opacity it was painted at, copies its
// commands, clips, gradients and glyphs and renumbers them; a build with
// nothing to repaint keeps the last list as it is.

#include "maul-ui/draw.h"

#include "context.h"
#include "draw_store.h"
#include "draw_transform.h"
#include "layer.h"
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
    // What the root of the walk under way reads as its parent's state:
    // the origin 0 for the list's root, its laid-out place for a layer;
    // no clip, full opacity.
    muiPaintState top;
    // The nodes painted rather than copied (muiWorkCounts).
    uint64_t painted;
} Build;

// Where a copied subtree's indices land: its own clips, gradients and
// transforms move by the difference of their first entries, and the clip
// and transform it was painted in become those it is painted in now.
typedef struct Renumbering
{
    muiDrawRange clips;
    uint32_t clipBase;
    uint32_t entryClip;
    muiDrawRange gradients;
    uint32_t gradientBase;
    muiDrawRange transforms;
    uint32_t transformBase;
    uint32_t entryTransform;
} Renumbering;

// An index of a span moved to base, or entry for one outside the span.
static uint32_t Moved(muiDrawRange own, uint32_t base, uint32_t entry, uint32_t index)
{
    return index >= own.first && index < own.end ? index - own.first + base : entry;
}

static uint32_t ClipOf(const Renumbering* renumbering, uint32_t clip)
{
    return Moved(renumbering->clips, renumbering->clipBase, renumbering->entryClip, clip);
}

static uint32_t TransformOf(const Renumbering* renumbering, uint32_t transform)
{
    return Moved(renumbering->transforms, renumbering->transformBase, renumbering->entryTransform,
                 transform);
}

// Whether a node's subtree can be taken from the last list.
static bool CanCopy(const Build* build, uint32_t slot, const muiPaintState* old, float x, float y,
                    float inherited)
{
    const muiTree* tree = &build->painter.context->tree;
    return build->previous != nullptr && old->build == build->previousBuild &&
           (muiTreeAt(tree, slot)->dirty.subtree & mui_stagePaint) == 0 && old->x == x &&
           old->y == y && old->inherited == inherited && old->culled == 0;
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
        command->transform = TransformOf(renumbering, command->transform);
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
        clip->transform = TransformOf(renumbering, clip->transform);
    }
    for (uint32_t i = old->transforms.first; i < old->transforms.end; i++)
    {
        uint32_t at = to->transformCount++;
        to->transformOwners[at] = from->transformOwners[i];
        to->transformParents[at] = TransformOf(renumbering, from->transformParents[i]);
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
        // A layer below was painted apart, with spans of its own that its
        // own walk takes.
        bool apart = muiIsLayerRoot(tree, at);
        if (!apart && state->build == build->previousBuild)
        {
            state->build = build->build;
            Shift(&state->commands, old->commands.first, now->commands.first);
            Shift(&state->clips, old->clips.first, now->clips.first);
            Shift(&state->gradients, old->gradients.first, now->gradients.first);
            state->transform =
                Moved(old->transforms, now->transforms.first, now->transform, state->transform);
            state->inner =
                Moved(old->transforms, now->transforms.first, now->transform, state->inner);
            Shift(&state->transforms, old->transforms.first, now->transforms.first);
        }
        // The next node in preorder below node.
        if (!apart && muiTreeAt(tree, at)->links.firstChild != 0)
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

// Takes a node's subtree, painted in entryClip and through
// entryTransform now, from the last list into state; false when it does
// not fit.
static bool Copy(Build* build, uint32_t node, muiPaintState* state, uint32_t entryClip,
                 uint32_t entryTransform)
{
    const muiPaintState old = *state;
    muiDrawTables* to = build->painter.out;
    muiPainter* painter = &build->painter;
    if (old.commands.end - old.commands.first > painter->commandCapacity - to->commandCount ||
        old.clips.end - old.clips.first > painter->clipCapacity - to->clipCount ||
        old.gradients.end - old.gradients.first > painter->gradientCapacity - to->gradientCount ||
        old.transforms.end - old.transforms.first > painter->transformCapacity - to->transformCount)
    {
        painter->full = true;
        return false;
    }
    const Renumbering renumbering = {old.clips,          to->clipCount,     entryClip,
                                     old.gradients,      to->gradientCount, old.transforms,
                                     to->transformCount, entryTransform};
    state->commands = (muiDrawRange){to->commandCount, 0};
    state->clips = (muiDrawRange){to->clipCount, 0};
    state->gradients = (muiDrawRange){to->gradientCount, 0};
    state->transforms = (muiDrawRange){to->transformCount, 0};
    state->transform = entryTransform;
    state->inner = TransformOf(&renumbering, old.inner);
    if (!CopyCommands(build, &old, &renumbering))
    {
        return false;
    }
    state->commands.end = to->commandCount;
    state->clips.end = to->clipCount;
    state->gradients.end = to->gradientCount;
    state->transforms.end = to->transformCount;
    state->build = build->build;
    Renumber(build, node, &old, state);
    return true;
}

// Paints or copies a node from its parent's state; true when its
// children are to be visited.
static bool Visit(Build* build, const muiPaintState* top, uint32_t root, uint32_t at)
{
    const muiContext* context = build->painter.context;
    const muiTree* tree = &context->tree;
    uint32_t parent = at == root ? 0 : muiTreeAt(tree, at)->links.parent;
    const muiPaintState* above = parent != 0 ? &build->store->states[parent - 1] : top;
    const muiRect* rect = &context->layout[at - 1].rect;
    muiPaintState* state = &build->store->states[at - 1];
    float x = above->x + rect->x;
    float y = above->y + rect->y;
    uint32_t clip = above->clip;
    uint32_t transform = above->inner;
    float inherited = above->opacity;
    state->culledBefore = build->painter.culled;
    if (CanCopy(build, at, state, x, y, inherited))
    {
        (void)Copy(build, at, state, clip, transform);
        return false;
    }
    build->painted++;
    // Field by field: a whole struct built on the stack and copied stalls
    // on reading back its narrower stores.
    const muiDrawTables* out = build->painter.out;
    state->build = build->build;
    state->rect = *rect;
    state->x = x;
    state->y = y;
    state->clip = clip;
    state->opacity = 0.0f;
    state->inherited = inherited;
    state->commands = (muiDrawRange){out->commandCount, 0};
    state->clips = (muiDrawRange){out->clipCount, 0};
    state->gradients = (muiDrawRange){out->gradientCount, 0};
    state->transform = transform;
    state->inner = transform;
    state->transforms = (muiDrawRange){out->transformCount, 0};
    bool painted = muiPaintNode(&build->painter, at, state);
    if (painted && build->painter.paint != nullptr)
    {
        muiPaintHostContent(&build->painter, at, state);
    }
    // Drawn through its scale, it is painted in its parent's: the state
    // keeps that one, as a copy of it does.
    state->transform = transform;
    return painted;
}

// Ends a node's spans where its subtree ended.
static void Leave(Build* build, uint32_t at)
{
    muiPaintState* state = &build->store->states[at - 1];
    const muiDrawTables* out = build->painter.out;
    state->commands.end = out->commandCount;
    state->clips.end = out->clipCount;
    state->gradients.end = out->gradientCount;
    state->transforms.end = out->transformCount;
    state->culled = build->painter.culled - state->culledBefore;
}

// The first of a node and its next siblings that roots no layer, or 0.
static uint32_t SkipLayers(const muiTree* tree, uint32_t at)
{
    while (at != 0 && muiIsLayerRoot(tree, at))
    {
        at = muiTreeAt(tree, at)->links.next;
    }
    return at;
}

// Paints a subtree in preorder; a layer below its root is left for a
// walk of its own.
static void Walk(Build* build, uint32_t root)
{
    const muiTree* tree = &build->painter.context->tree;
    const muiPaintState top = build->top;
    for (uint32_t at = root; at != 0 && !build->painter.full;)
    {
        if (Visit(build, &top, root, at))
        {
            uint32_t child = SkipLayers(tree, muiTreeAt(tree, at)->links.firstChild);
            if (child != 0)
            {
                at = child;
                continue;
            }
        }
        for (;;)
        {
            Leave(build, at);
            if (at == root)
            {
                at = 0;
                break;
            }
            uint32_t next = SkipLayers(tree, muiTreeAt(tree, at)->links.next);
            if (next != 0)
            {
                at = next;
                break;
            }
            at = muiTreeAt(tree, at)->links.parent;
        }
    }
}

// How many transforms a node owns: one for a scale, one for scrolling.
static uint32_t TransformsOf(const muiContext* context, uint32_t slot)
{
    const muiLocalScale* scale = &context->visual[slot - 1].scale;
    return (scale->x != 1.0f || scale->y != 1.0f ? 1u : 0u) +
           (context->layout[slot - 1].style.scrollAxes != mui_scrollNone ? 1u : 0u);
}

// The transform a layer's parent's children go through, so that the
// layer scrolls and scales with them: the parent's, or for a parent not
// painted now (in a layer above, or below a node that draws nothing) its
// and its ancestors' transforms up to one that was, added again: their
// values come from the same owners.
static uint32_t ParentTransform(Build* build, uint32_t root, uint32_t parent)
{
    const muiContext* context = build->painter.context;
    const muiTree* tree = &context->tree;
    const muiPaintState* states = build->store->states;
    uint32_t count = 0;
    uint32_t at = parent;
    while (at != 0 && states[at - 1].build != build->build)
    {
        count += TransformsOf(context, at);
        at = at == root ? 0 : muiTreeAt(tree, at)->links.parent;
    }
    uint32_t base = at != 0 ? states[at - 1].inner : 0;
    muiDrawTables* out = build->painter.out;
    if (count == 0 || count > build->painter.transformCapacity - out->transformCount)
    {
        build->painter.full = count != 0;
        return base;
    }
    // From the bottom up, each entry after the one above it: a node's
    // scroll after its scale.
    uint32_t first = out->transformCount;
    uint32_t next = first + count;
    out->transformCount = next;
    for (at = parent; next > first; at = muiTreeAt(tree, at)->links.parent)
    {
        const muiLocalScale* scale = &context->visual[at - 1].scale;
        const uint32_t owners[2] = {
            context->layout[at - 1].style.scrollAxes != mui_scrollNone ? at : 0,
            scale->x != 1.0f || scale->y != 1.0f ? at | MUI_TRANSFORM_SCALE : 0,
        };
        for (int i = 0; i < 2; i++)
        {
            if (owners[i] != 0)
            {
                next--;
                out->transformOwners[next] = owners[i];
                out->transformParents[next] = next == first ? base : next - 1;
            }
        }
    }
    return first + count - 1;
}

// The next layer below the root to walk after place, from the bottom,
// with its parent's origin set as the walk's; 0 after the last.
static uint32_t NextLayer(Build* build, uint32_t root, uint32_t* place)
{
    const muiContext* context = build->painter.context;
    while (*place < context->layers.count)
    {
        uint32_t layer = muiLayerAt(context, (*place)++);
        if (layer != 0 && layer != root && muiTreeIsAncestor(&context->tree, root, layer))
        {
            muiParentOrigin(context, root, layer, &build->top.x, &build->top.y);
            build->top.inner =
                ParentTransform(build, root, muiTreeAt(&context->tree, layer)->links.parent);
            return layer;
        }
    }
    return 0;
}

// Paints the root's subtree without its layers, then each layer below it
// from the bottom: at its laid-out place, outside its ancestors' clips and
// opacity. One call of Walk, so it stays inline.
static void WalkAll(Build* build, uint32_t root)
{
    uint32_t place = 0;
    for (uint32_t at = root; at != 0 && !build->painter.full; at = NextLayer(build, root, &place))
    {
        Walk(build, at);
    }
}

static void Empty(muiDrawTables* tables)
{
    tables->commandCount = 0;
    tables->clipCount = 1;
    tables->gradientCount = 1;
    tables->glyphCount = 0;
    tables->transformCount = 1;
}

// Whether the last list stands for this build: of this root, surface and
// scale, with no paint requested below the root, nor scrolling where
// host content culled to what was visible. A failed build left a header
// of scale 0, which no build has. A new node in the root's slot is
// requested every stage until a build of it, which records it.
static bool IsUnchanged(const muiContext* context, uint32_t root, const muiDrawInput* input)
{
    const muiDrawStore* store = &context->draw;
    return store->rootIndex == root && store->header.surface == input->surface &&
           store->header.scale == input->scale &&
           (muiTreeAt(&context->tree, root)->dirty.subtree & mui_stagePaint) == 0 &&
           (!context->scrolled || store->culled == 0);
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
    if (IsUnchanged(context, root, input))
    {
        // Scrolling alone moves the transforms: a list of its own, the same
        // commands.
        if (context->scrolled)
        {
            muiSetTransforms(context, root, &store->tables[store->current], input->scale);
            store->header.generation++;
            context->scrolled = false;
        }
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
    build.painter.transformCapacity = store->transformCapacity;
    build.painter.paint = input->paint;
    build.painter.paintUser = input->paintUser;
    build.painter.scale = input->scale;
    build.painter.root = root;
    build.store = store;
    build.previous = takes ? &store->tables[store->current] : nullptr;
    build.previousBuild = store->header.generation;
    build.build = store->header.generation + 1;
    build.top.opacity = 1.0f;
    Empty(build.painter.out);
    // The host's paint function may read the context, not edit it.
    context->inHostCall = true;
    WalkAll(&build, root);
    context->inHostCall = false;
    context->misuse += build.painter.misuse;
    context->work.painted += build.painted;
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
    store->culled = build.painter.culled;
    muiSetTransforms(context, root, build.painter.out, input->scale);
    context->scrolled = false;
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
        .transforms = tables->transforms,
        .transformCount = tables->transformCount,
        .gradientCount = tables->gradientCount,
        .gradients = tables->gradients,
        .glyphs = tables->glyphs,
        .glyphCount = tables->glyphCount,
    };
    return mui_success;
}
