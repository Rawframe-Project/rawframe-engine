// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The cache answers a sizing query when a stored result provably answers
// it the same; everything else is computed and stored.

#include "solve.h"

#include "absolute.h"
#include "condition.h"
#include "flex.h"
#include "fpenv.h"
#include "invariant.h"
#include "sizing.h"

#include <math.h>

static bool IsSized(muiMeasureMode mode)
{
    return mode == mui_measureExact || mode == mui_measureAtMost;
}

// Whether a size computed under an old constraint answers a new one, by
// the rules Yoga's cache uses: the same constraint; an exact size equal to
// what an unshrunk sizing gave; a max-content size that fits the new
// space; or a smaller space the old size still fits. The two that take a
// content size for an exact or limited one hold only while it is the
// content's own (loose).
static bool AxisAnswers(muiMeasureAxis next, muiMeasureAxis old, float result, bool loose)
{
    if (next.mode == old.mode && (!IsSized(next.mode) || next.size == old.size))
    {
        return true;
    }
    if (next.mode == mui_measureExact)
    {
        return loose && old.mode != mui_measureMinContent && next.size == result;
    }
    if (next.mode == mui_measureAtMost)
    {
        bool fitsMaxContent = loose && old.mode == mui_measureMaxContent;
        bool stricter = old.mode == mui_measureAtMost && next.size < old.size;
        return (fitsMaxContent || stricter) && result <= next.size;
    }
    return false;
}

// Whether a node's own sizing reads its parent's extents: a scaled size or
// limit, which the node resolves against the extents in its input (a
// parent resolves a scaled size too, but a query it leaves open, as with
// an aspect ratio, still reaches the node's own).
static bool ReadsParentExtent(const muiSizing* sizing)
{
    return muiIsScaled(sizing->width) || muiIsScaled(sizing->height) ||
           muiIsScaled(sizing->minWidth) || muiIsScaled(sizing->maxWidth) ||
           muiIsScaled(sizing->minHeight) || muiIsScaled(sizing->maxHeight);
}

// Whether a node's own minimum or maximum limits it along an axis.
static bool IsLimited(const muiSizing* sizing, bool horizontal)
{
    return horizontal ? sizing->minWidth.kind != mui_dimensionAuto ||
                            sizing->maxWidth.kind != mui_dimensionAuto
                      : sizing->minHeight.kind != mui_dimensionAuto ||
                            sizing->maxHeight.kind != mui_dimensionAuto;
}

// On which axes a node's content size is the content's own (the cache's
// loose bits): not where its minimum or maximum may have clamped it, as
// text held to a maximum width is one line long at max-content and wraps
// at that width; nor anywhere when the node has an aspect ratio, which
// gives one axis from the other only when that one is definite, or when
// sizes below take a definite size above them (muiScaledBelow).
static unsigned LooseOf(const muiSolver* solver, uint32_t node)
{
    muiLayoutNode* layout = &solver->nodes[node - 1];
    if (layout->cache.loose == 0)
    {
        bool scaled = layout->style.sizing.aspectRatio != 0.0f ||
                      muiScaledBelow(solver->tree, solver->nodes, node);
        const muiSizing* sizing = &layout->style.sizing;
        layout->cache.loose = (uint8_t)(4u | (!scaled && !IsLimited(sizing, true) ? 1u : 0u) |
                                        (!scaled && !IsLimited(sizing, false) ? 2u : 0u));
    }
    return layout->cache.loose;
}

static const muiCacheEntry* FindCached(const muiSolver* solver, uint32_t node,
                                       const muiSizingInput* input)
{
    const muiLayoutNode* layout = &solver->nodes[node - 1];
    bool keyExtents = ReadsParentExtent(&layout->style.sizing);
    // Only a row with children reads whether its height is loose
    // (src/flex.c); for any other node the flag would split its entries.
    muiFlexDirection direction = layout->style.container.direction;
    bool keyLoose = (direction == mui_flexRow || direction == mui_flexRowReverse) &&
                    muiTreeAt(solver->tree, node)->links.firstChild != 0;
    unsigned loose = LooseOf(solver, node);
    for (int i = 0; i < MUI_CACHE_ENTRIES; i++)
    {
        const muiCacheEntry* entry = &layout->cache.entries[i];
        bool sameExtents = !keyExtents || (entry->input.parentWidth == input->parentWidth &&
                                           entry->input.parentHeight == input->parentHeight);
        // Direction is part of the key: a safe area on a start or end edge
        // below can change a size with it, though most direction moves
        // children alone; so is whether a height is the content's own.
        if (entry->valid && sameExtents && entry->input.rtl == input->rtl &&
            entry->input.contentHeight == input->contentHeight &&
            entry->input.contentOnly == input->contentOnly &&
            entry->input.contentOnlyHeight == input->contentOnlyHeight &&
            entry->input.ratioPercent == input->ratioPercent &&
            entry->input.ratioLines == input->ratioLines &&
            (!keyLoose || entry->input.looseHeight == input->looseHeight) &&
            AxisAnswers(input->width, entry->input.width, entry->size.width, (loose & 1u) != 0) &&
            AxisAnswers(input->height, entry->input.height, entry->size.height, (loose & 2u) != 0))
        {
            return entry;
        }
    }
    return nullptr;
}

static void StoreCached(muiLayoutCache* cache, const muiSizingInput* input, muiSize size)
{
    cache->replaced = cache->replaced || cache->entries[cache->next].valid;
    cache->entries[cache->next] = (muiCacheEntry){.input = *input, .size = size, .valid = true};
    cache->next = (uint8_t)((cache->next + 1) % MUI_CACHE_ENTRIES);
}

// The content-box constraint for host content from a border-box one.
static muiMeasureAxis ContentAxis(muiMeasureAxis axis, float box)
{
    if (IsSized(axis.mode))
    {
        return (muiMeasureAxis){fmaxf(axis.size - box, 0.0f), axis.mode};
    }
    return axis;
}

static float SaneLength(float value)
{
    return isfinite(value) && value > 0.0f ? value : 0.0f;
}

// The host's measure function, run in the host's floating-point
// environment.
static MUI_NOINLINE muiSize HostMeasure(const muiSolver* solver, muiNodeId id, uint64_t hostKey,
                                        muiMeasureAxis width, muiMeasureAxis height)
{
    muiFloatHost host = muiFloatHostEnter();
    muiSize size = solver->measure(solver->user, id, hostKey, width, height);
    muiFloatHostLeave(host);
    return size;
}

// Notes host content that could not be measured: laid out as empty now,
// and measured again by the next layout.
static void Unmeasured(const muiSolver* solver, uint32_t node)
{
    solver->nodes[node - 1].unmeasured = true;
    (*solver->unmeasured)++;
}

// The host's baseline function, run in the host's floating-point
// environment.
static MUI_NOINLINE float HostBaseline(const muiSolver* solver, muiNodeId id, uint64_t hostKey,
                                       float width, float height)
{
    muiFloatHost host = muiFloatHostEnter();
    float baseline = solver->measureBaseline(solver->user, id, hostKey, width, height);
    muiFloatHostLeave(host);
    return baseline;
}

static muiSize SizeLeaf(const muiSolver* solver, uint32_t node, const muiSizingInput* input)
{
    const muiLayoutStyle* style = &solver->nodes[node - 1].style;
    const muiEdges* padding = &solver->paddings[node - 1];
    float boxWidth = muiBoxSum(padding, style, true);
    float boxHeight = muiBoxSum(padding, style, false);
    muiSize content = {0.0f, 0.0f};
    // Both sizes exact decide the size, as the final pass always gives
    // them: the host is not asked.
    bool decided = input->width.mode == mui_measureExact && input->height.mode == mui_measureExact;
    muiAxisSizing width = muiResolveAxis(&style->sizing, true, input->parentWidth);
    muiAxisSizing height = muiResolveAxis(&style->sizing, false, input->parentHeight);
    if (input->contentOnly)
    {
        *(input->contentOnlyHeight ? &height : &width) = (muiAxisSizing){.maximum = INFINITY};
    }
    if (style->content == mui_contentHost && solver->measure != nullptr && !decided)
    {
        // The content is laid out within the width the node ends at, which
        // its minimum floors and its maximum caps: text measured in other
        // space would keep the height of other lines than it shows.
        muiMeasureAxis across = input->width;
        float floor = fmaxf(width.minimum, boxWidth);
        float cap = fmaxf(width.maximum, floor);
        if (across.mode == mui_measureAtMost)
        {
            across.size = fminf(fmaxf(across.size, floor), cap);
        }
        else if (across.mode == mui_measureMaxContent && cap < INFINITY)
        {
            across = (muiMeasureAxis){cap, mui_measureAtMost};
        }
        muiNodeId id = muiTreeIdOf(solver->tree, node);
        uint64_t hostKey = muiTreeAt(solver->tree, node)->hostKey;
        solver->work->measured++;
        content = HostMeasure(solver, id, hostKey, ContentAxis(across, boxWidth),
                              ContentAxis(input->height, boxHeight));
        if (isnan(content.width) || isnan(content.height))
        {
            Unmeasured(solver, node);
        }
        content.width = SaneLength(content.width);
        content.height = SaneLength(content.height);
    }
    muiSize size;
    size.width =
        input->width.mode == mui_measureExact
            ? input->width.size
            : muiClampSize(content.width + boxWidth, width.minimum, width.maximum, boxWidth);
    size.height =
        input->height.mode == mui_measureExact
            ? input->height.size
            : muiClampSize(content.height + boxHeight, height.minimum, height.maximum, boxHeight);
    return size;
}

// Mirrors the children's horizontal positions in a container of width.
static void Mirror(const muiSolver* solver, uint32_t node, float width)
{
    for (uint32_t c = muiTreeAt(solver->tree, node)->links.firstChild; c != 0;
         c = muiTreeAt(solver->tree, c)->links.next)
    {
        // A popped child keeps where it was drawn (src/absolute.c).
        muiRect* rect = &solver->nodes[c - 1].rect;
        rect->x = solver->nodes[c - 1].popped ? rect->x : width - rect->x - rect->width;
    }
}

// Requests paint on the children whose rectangles are not what was last
// painted: a draw list built from the last one copies only what did not
// move.
static void MarkMoved(const muiSolver* solver, uint32_t node)
{
    for (uint32_t c = muiTreeAt(solver->tree, node)->links.firstChild; c != 0;
         c = muiTreeAt(solver->tree, c)->links.next)
    {
        if (!muiIsSameRect(solver->nodes[c - 1].rect, solver->painted[c - 1].rect))
        {
            muiTreeMark(solver->restyle, c, mui_stagePaint);
        }
    }
}

// The furthest left, right and bottom of the border boxes below a node,
// in or out of the flow, from its border box's origin; a scroll
// container below clips its own (CSS Overflow 3's scrollable overflow).
typedef struct
{
    float left;
    float right;
    float top;
    float bottom;
} Reach;

// Takes a border box at left, top into a reach: a box with no area
// adds nothing, as in Chrome and Firefox, though what is below it may.
static void Unite(Reach* reach, float left, float top, muiRect rect)
{
    if (rect.width > 0.0f && rect.height > 0.0f)
    {
        reach->left = fminf(reach->left, left);
        reach->right = fmaxf(reach->right, left + rect.width);
        reach->top = fminf(reach->top, top);
        reach->bottom = fmaxf(reach->bottom, top + rect.height);
    }
}

static void ReachBelow(const muiSolver* solver, uint32_t node, float x, float y, Reach* reach)
{
    for (uint32_t c = muiTreeAt(solver->tree, node)->links.firstChild; c != 0;
         c = muiTreeAt(solver->tree, c)->links.next)
    {
        if (solver->nodes[c - 1].listed)
        {
            // Placed after layout: its list's own extent holds it.
            continue;
        }
        const muiRect* rect = &solver->nodes[c - 1].rect;
        float left = x + rect->x;
        float top = y + rect->y;
        Unite(reach, left, top, *rect);
        if (solver->nodes[c - 1].style.scrollAxes == mui_scrollNone)
        {
            ReachBelow(solver, c, left, top, reach);
        }
    }
}

// A scroll container's extent as it is measured: each axis from its
// scroll origin, x mirrored from the right, y from the bottom; the
// furthest flow (margin boxes) and box (border boxes) ends so far. Far
// is the side away from the origin; farStart says the inline start is
// (the right under rtl).
typedef struct
{
    muiSize size;
    bool rtl;
    bool mirrored;
    bool right;
    bool bottom;
    bool farStart;
    float flowX;
    float flowY;
    float boxX;
    float boxY;
} Extents;

// Takes a child's reach into the extents.
static void AddChild(const muiSolver* solver, uint32_t c, Extents* e)
{
    const muiLayoutNode* child = &solver->nodes[c - 1];
    // Logical under rtl until mirrored; a popped child is where it was
    // drawn. Mirrored again from the right.
    float width = child->rect.width;
    float height = child->rect.height;
    float x = e->rtl && !e->mirrored && !child->popped ? e->size.width - child->rect.x - width
                                                       : child->rect.x;
    x = e->right ? e->size.width - x - width : x;
    float y = e->bottom ? e->size.height - child->rect.y - height : child->rect.y;
    if (!child->absolute)
    {
        // A negative end margin pulls its end back at most to its start
        // (as Chrome and Firefox).
        muiEdges margins = muiMarginsOf(&child->style, e->rtl);
        e->flowX =
            fmaxf(e->flowX, x + fmaxf(width + (e->farStart ? margins.start : margins.end), 0.0f));
        e->flowY =
            fmaxf(e->flowY, y + fmaxf(height + (e->bottom ? margins.top : margins.bottom), 0.0f));
    }
    // What is below the child is in physical coordinates: from the right
    // or the bottom, its far end is its leftmost or topmost box.
    Reach reach = {INFINITY, -INFINITY, INFINITY, -INFINITY};
    Unite(&reach, 0.0f, 0.0f, child->rect);
    if (child->style.scrollAxes == mui_scrollNone)
    {
        ReachBelow(solver, c, 0.0f, 0.0f, &reach);
    }
    e->boxX = fmaxf(e->boxX, x + (e->right ? width - reach.left : reach.right));
    e->boxY = fmaxf(e->boxY, y + (e->bottom ? height - reach.top : reach.bottom));
}

// A scroll container's extent: the furthest end of its flow children's
// margin boxes plus its end padding, or of any border box below it, from
// its padding box's start, at least the padding box (as Chrome and
// Firefox), each axis from its scroll origin, which it records; its
// offsets are brought within it, rtl its own direction. Its children are
// in logical coordinates unless mirrored says layout already mirrored
// them.
static void MeasureExtent(const muiSolver* solver, uint32_t node, muiSize size, bool rtl,
                          bool mirrored)
{
    const muiLayoutStyle* style = &solver->nodes[node - 1].style;
    const muiEdges* padding = &solver->paddings[node - 1];
    muiFlexDirection direction = style->container.direction;
    bool row = direction == mui_flexRow || direction == mui_flexRowReverse;
    bool wraps = style->container.wrap == mui_wrapReverse;
    bool right = rtl != (row ? direction == mui_flexRowReverse : wraps);
    Extents e = {
        .size = size,
        .rtl = rtl,
        .mirrored = mirrored,
        .right = right,
        .bottom = row ? wraps : direction == mui_flexColumnReverse,
        .farStart = right != rtl,
    };
    float padX = e.farStart ? padding->start : padding->end;
    float padY = e.bottom ? padding->top : padding->bottom;
    // The padding box's far edges, from the origin.
    float edgeX = size.width - (e.farStart ? style->border.start : style->border.end);
    float edgeY = size.height - (e.bottom ? style->border.top : style->border.bottom);
    e.flowX = edgeX - padX;
    e.flowY = edgeY - padY;
    for (uint32_t c = muiTreeAt(solver->tree, node)->links.firstChild; c != 0;
         c = muiTreeAt(solver->tree, c)->links.next)
    {
        // A listed child is placed along its list after layout: the list's
        // length below reaches past it, and across it the content box
        // holds it.
        if (!solver->nodes[c - 1].listed)
        {
            AddChild(solver, c, &e);
        }
    }
    muiScrollState* scroll = &solver->scrolls[node - 1];
    scroll->right = e.right;
    scroll->bottom = e.bottom;
    float nearX = e.farStart ? style->border.end : style->border.start;
    float nearY = e.bottom ? style->border.bottom : style->border.top;
    float flowX = e.flowX + padX;
    float flowY = e.flowY + padY;
    if (!(flowX > nearX && flowY > nearY))
    {
        // The padding box and the flow with no area reach nothing past
        // the padding box, as a box with none (Chrome and Firefox).
        flowX = edgeX;
        flowY = edgeY;
    }
    scroll->reachWidth = fmaxf(flowX, e.boxX) - nearX;
    scroll->reachHeight = fmaxf(flowY, e.boxY) - nearY;
    // A virtual list's items reach as far as they need, realized or not.
    scroll->extentWidth = fmaxf(scroll->reachWidth, padding->start + scroll->listX + padding->end);
    scroll->extentHeight =
        fmaxf(scroll->reachHeight, padding->top + scroll->listY + padding->bottom);
    // At the size given here: its parent sets its rectangle after this.
    scroll->x = fminf(scroll->x, muiScrollLimit(style, size, scroll, true));
    scroll->y = fminf(scroll->y, muiScrollLimit(style, size, scroll, false));
}

void muiMeasureExtentAbove(const muiSolver* solver, uint32_t root, uint32_t node)
{
    for (uint32_t at = node; at != root;)
    {
        at = muiTreeAt(solver->tree, at)->links.parent;
        const muiLayoutNode* layout = &solver->nodes[at - 1];
        if (layout->style.scrollAxes != mui_scrollNone)
        {
            // Nothing painted reads the extent, and a draw list takes the
            // offsets as it is built.
            MeasureExtent(solver, at, (muiSize){layout->rect.width, layout->rect.height},
                          layout->rtl, true);
            return;
        }
    }
}

// Lays a container out in logical coordinates, start on the left, and
// mirrors it when its direction is right to left.
static muiSize SizeContainer(const muiSolver* solver, uint32_t node, const muiSizingInput* input,
                             bool perform)
{
    muiSize size = muiLayoutFlex(solver, node, input, perform);
    if (perform)
    {
        muiPlaceAbsolute(solver, node, size, input->rtl);
        if (solver->nodes[node - 1].style.scrollAxes != mui_scrollNone)
        {
            MeasureExtent(solver, node, size, input->rtl, false);
        }
        if (input->rtl)
        {
            Mirror(solver, node, size.width);
        }
        MarkMoved(solver, node);
    }
    return size;
}

// The node's answer to a query, computed without its aspect ratio.
static muiSize ContentAnswer(const muiSolver* solver, uint32_t node, const muiSizingInput* input)
{
    return muiTreeAt(solver->tree, node)->links.firstChild == 0
               ? SizeLeaf(solver, node, input)
               : muiLayoutFlex(solver, node, input, false);
}

// The node's min-content size along an axis, the other axis as input
// gives it, computed without its aspect ratio.
static float ContentSize(const muiSolver* solver, uint32_t node, const muiSizingInput* input,
                         bool horizontal)
{
    muiSizingInput probe = *input;
    muiMeasureAxis* axis = horizontal ? &probe.width : &probe.height;
    *axis = (muiMeasureAxis){0.0f, mui_measureMinContent};
    muiSize size = ContentAnswer(solver, node, &probe);
    return horizontal ? size.width : size.height;
}

// The node's content height at the width input gives, its height to come
// from its aspect ratio as size: a row is laid out at that height, which
// its items' percentages resolve against; a column or a leaf measures its
// content.
static float RatioContentHeight(const muiSolver* solver, uint32_t node, const muiSizingInput* input,
                                float size)
{
    muiFlexDirection direction = solver->nodes[node - 1].style.container.direction;
    if (muiTreeAt(solver->tree, node)->links.firstChild == 0 ||
        (direction != mui_flexRow && direction != mui_flexRowReverse))
    {
        // Its items' percentages against the ratio's height.
        muiSizingInput probe = *input;
        probe.ratioPercent = true;
        return ContentSize(solver, node, &probe, false);
    }
    // A row laid out at the ratio's height, definite for its items.
    muiSizingInput probe = *input;
    probe.height = muiExact(size);
    probe.contentHeight = false;
    return muiFlexRatioContentHeight(solver, node, &probe);
}

// Whether a node is a column that wraps, with items to wrap.
static bool IsWrappingColumn(const muiSolver* solver, uint32_t node)
{
    const muiFlexContainer* container = &solver->nodes[node - 1].style.container;
    return muiTreeAt(solver->tree, node)->links.firstChild != 0 &&
           container->wrap != mui_wrapNone &&
           (container->direction == mui_flexColumn ||
            container->direction == mui_flexColumnReverse);
}

// The size the aspect ratio gives an axis from the other one's, at least
// the content's min-content size when the axis's minimum is automatic
// and the node is not a scroll container (CSS Sizing 4; scrolling on
// either axis makes it one). A width given as a percentage that cannot
// resolve takes that minimum too; a height so given does not, as in
// Chrome. Within its limits.
static muiMeasureAxis RatioAxis(const muiSolver* solver, uint32_t node, const muiSizingInput* input,
                                bool horizontal, float size)
{
    const muiLayoutStyle* style = &solver->nodes[node - 1].style;
    muiAxisSizing axis = muiResolveAxis(&style->sizing, horizontal,
                                        horizontal ? input->parentWidth : input->parentHeight);
    if (axis.minimumAuto && style->scrollAxes == mui_scrollNone &&
        (horizontal || style->sizing.height.kind == mui_dimensionAuto))
    {
        // A row's stretched items take a height from the ratio, so they
        // leave its floor; asked its min-content height, as a column
        // measures its items, the node counts its content in full.
        // A wrapping column so asked breaks its lines at the ratio's
        // height all the same (as Chrome).
        bool full = horizontal || input->height.mode == mui_measureMinContent;
        muiSizingInput lines = *input;
        lines.ratioLines = !horizontal && IsWrappingColumn(solver, node);
        float floor = full ? ContentSize(solver, node, &lines, horizontal)
                           : RatioContentHeight(solver, node, input, size);
        muiAxisSizing height = muiResolveAxis(&style->sizing, false, input->parentHeight);
        if (horizontal && !height.definite)
        {
            // A width's floor is capped by the height's maximum through
            // the ratio, a height not given, and by the height a column's
            // flexing fixes, not an absolute box's insets (as Chrome; a
            // height's floor is not capped by the width's).
            float box = muiBoxSum(&solver->paddings[node - 1], style, false);
            float ceiling = muiAxisCeiling(&height, box);
            if (input->height.mode == mui_measureExact && !solver->nodes[node - 1].absolute)
            {
                ceiling = fminf(ceiling, input->height.size);
            }
            floor = fminf(floor, ceiling * style->sizing.aspectRatio);
        }
        size = fmaxf(size, floor);
    }
    return muiExact(muiClampSize(size, axis.minimum, axis.maximum,
                                 muiBoxSum(&solver->paddings[node - 1], style, horizontal)));
}

// With an aspect ratio, a node given an exact size on one axis and an
// automatic one on the other takes the other from the ratio; given
// neither, its content's width within the ratio's limits.
static void ApplyAspectRatio(const muiSolver* solver, uint32_t node, muiSizingInput* input)
{
    const muiSizing* sizing = &solver->nodes[node - 1].style.sizing;
    float ratio = sizing->aspectRatio;
    if (ratio <= 0.0f)
    {
        return;
    }
    muiAxisSizing width = muiResolveAxis(sizing, true, input->parentWidth);
    muiAxisSizing height = muiResolveAxis(sizing, false, input->parentHeight);
    bool exactWidth = input->width.mode == mui_measureExact;
    bool exactHeight = input->height.mode == mui_measureExact;
    if (exactWidth && !exactHeight && !height.definite)
    {
        input->height = RatioAxis(solver, node, input, false, input->width.size / ratio);
    }
    else if (exactHeight && !exactWidth && !width.definite)
    {
        input->width = RatioAxis(solver, node, input, true, input->height.size * ratio);
        if (muiRatioWidthMinimum(&solver->nodes[node - 1].style) && !height.definite)
        {
            // A percentage height with nothing to resolve against, which
            // a column flexed to an exact size: the width through the
            // ratio is at least the content's min-content width, an
            // automatic minimum a given minimum width replaces (as Chrome
            // and Firefox); the maximum width still clamps it.
            muiSizingInput probe = *input;
            probe.width = (muiMeasureAxis){0.0f, mui_measureMinContent};
            input->width.size = fmaxf(input->width.size, ContentAnswer(solver, node, &probe).width);
        }
    }
    else if (!exactWidth && !exactHeight && !width.definite && !height.definite)
    {
        // Neither size known: the width is its content's within the
        // height's limits through the ratio, padding and border among
        // them, and the height follows, as in CSS Sizing 4 and Chrome.
        const muiLayoutStyle* style = &solver->nodes[node - 1].style;
        float box = muiBoxSum(&solver->paddings[node - 1], style, false);
        float size = ContentAnswer(solver, node, input).width;
        size = fminf(fmaxf(size, fmaxf(height.minimum, box) * ratio),
                     muiAxisCeiling(&height, box) * ratio);
        size = muiClampSize(size, width.minimum, width.maximum,
                            muiBoxSum(&solver->paddings[node - 1], style, true));
        input->width = muiExact(size);
        input->height = RatioAxis(solver, node, input, false, size / ratio);
    }
}

// The input with the node's own direction in place of the inherited one.
static muiSizingInput OwnDirection(const muiLayoutStyle* style, const muiSizingInput* input)
{
    muiSizingInput own = *input;
    own.rtl = muiIsRtl(style, input->rtl);
    return own;
}

static bool IsSameSize(muiSize a, muiSize b)
{
    return a.width == b.width && a.height == b.height;
}

// Records the direction a node was laid out in and, when its conditions
// read a size or direction this layout changed, requests its style for the
// next pass: conditions read the state before the pass. A held node is
// styled again only when its size leaves the two it oscillated between.
static void Published(const muiSolver* solver, uint32_t node, muiSize size, bool rtl)
{
    muiLayoutNode* layout = &solver->nodes[node - 1];
    // Corners and sides are physical in the draw list.
    if (rtl != layout->rtl)
    {
        muiTreeMark(solver->restyle, node, mui_stagePaint);
    }
    layout->rtl = rtl;
    if (layout->held)
    {
        if (IsSameSize(size, layout->heldSizes[0]) || IsSameSize(size, layout->heldSizes[1]))
        {
            return;
        }
        layout->held = false;
        muiTreeMark(solver->restyle, node, mui_stageStyle);
        return;
    }
    bool sizeChanged =
        (layout->conditionReads & mui_readsSize) != 0 &&
        (size.width != layout->conditionSize.width || size.height != layout->conditionSize.height);
    bool directionChanged =
        (layout->conditionReads & mui_readsDirection) != 0 && rtl != layout->conditionRtl;
    if (sizeChanged || directionChanged)
    {
        muiTreeMark(solver->restyle, node, mui_stageStyle);
    }
}

muiSize muiSolveNode(const muiSolver* solver, uint32_t node, const muiSizingInput* input,
                     bool perform)
{
    muiLayoutCache* cache = &solver->nodes[node - 1].cache;
    if (perform)
    {
        MUI_ASSERT(input->width.mode == mui_measureExact && input->height.mode == mui_measureExact);
        if (cache->finalValid && cache->finalRtl == input->rtl &&
            cache->finalContentHeight == input->contentHeight &&
            cache->finalRatioPercent == input->ratioPercent &&
            cache->finalRatioLines == input->ratioLines &&
            cache->finalLooseHeight == input->looseHeight &&
            cache->finalEarlyHeight == input->earlyHeight &&
            cache->finalSize.width == input->width.size &&
            cache->finalSize.height == input->height.size)
        {
            return cache->finalSize;
        }
    }
    else
    {
        // The parent gave both sizes: nothing below can change them.
        if (input->width.mode == mui_measureExact && input->height.mode == mui_measureExact)
        {
            return (muiSize){input->width.size, input->height.size};
        }
        const muiCacheEntry* hit = FindCached(solver, node, input);
        if (hit != nullptr)
        {
            return hit->size;
        }
    }
    solver->work->sized++;
    const muiLayoutStyle* style = &solver->nodes[node - 1].style;
    muiSizingInput own = OwnDirection(style, input);
    // Its padding with the safe area, in its direction, which its own
    // sizing reads and later readers find beside it.
    solver->paddings[node - 1] = muiPaddingOf(style, &solver->safeArea, own.rtl);
    if (!own.contentOnly)
    {
        ApplyAspectRatio(solver, node, &own);
    }
    muiSize size = muiTreeAt(solver->tree, node)->links.firstChild == 0
                       ? SizeLeaf(solver, node, &own)
                       : SizeContainer(solver, node, &own, perform);
    if (perform)
    {
        // A scroll container without children still has an extent, its
        // padding box with its list's reach, and its offsets within it.
        if (muiTreeAt(solver->tree, node)->links.firstChild == 0 &&
            style->scrollAxes != mui_scrollNone)
        {
            MeasureExtent(solver, node, size, own.rtl, false);
        }
        cache->finalValid = true;
        cache->finalRtl = input->rtl;
        cache->finalContentHeight = input->contentHeight;
        cache->finalRatioPercent = input->ratioPercent;
        cache->finalRatioLines = input->ratioLines;
        cache->finalLooseHeight = input->looseHeight;
        cache->finalEarlyHeight = input->earlyHeight;
        cache->finalSize = size;
        Published(solver, node, size, own.rtl);
    }
    else
    {
        StoreCached(cache, input, size);
    }
    return size;
}

// Host content's baseline from the border box's top; NaN without one.
static float LeafBaseline(const muiSolver* solver, uint32_t node, const muiSizingInput* input)
{
    const muiLayoutStyle* style = &solver->nodes[node - 1].style;
    if (style->content != mui_contentHost || solver->measureBaseline == nullptr)
    {
        return NAN;
    }
    const muiEdges padding = muiPaddingOf(style, &solver->safeArea, input->rtl);
    float top = muiEdgeStart(&padding, false) + muiEdgeStart(&style->border, false);
    float width = fmaxf(input->width.size - muiBoxSum(&padding, style, true), 0.0f);
    float height = fmaxf(input->height.size - muiBoxSum(&padding, style, false), 0.0f);
    muiNodeId id = muiTreeIdOf(solver->tree, node);
    uint64_t hostKey = muiTreeAt(solver->tree, node)->hostKey;
    float baseline = HostBaseline(solver, id, hostKey, width, height);
    if (baseline == INFINITY)
    {
        Unmeasured(solver, node);
        return NAN;
    }
    return top + baseline;
}

// A scroll container's baseline is held within its border box (CSS Box
// Alignment 3 section 9.1, as Chrome and Firefox).
float muiSolveBaseline(const muiSolver* solver, uint32_t node, const muiSizingInput* input)
{
    MUI_ASSERT(input->width.mode == mui_measureExact && input->height.mode == mui_measureExact);
    muiSizingInput own = OwnDirection(&solver->nodes[node - 1].style, input);
    float baseline = muiTreeAt(solver->tree, node)->links.firstChild == 0
                         ? LeafBaseline(solver, node, &own)
                         : muiFlexBaseline(solver, node, &own);
    if (!isfinite(baseline))
    {
        return input->height.size;
    }
    return solver->nodes[node - 1].style.scrollAxes != mui_scrollNone
               ? fminf(fmaxf(baseline, 0.0f), input->height.size)
               : baseline;
}

void muiRatioRootWidth(const muiSolver* solver, uint32_t root, muiSizingInput* input)
{
    const muiLayoutStyle* style = &solver->nodes[root - 1].style;
    if (muiRatioWidthMinimum(style) && input->width.mode == mui_measureExact &&
        input->height.mode == mui_measureExact)
    {
        muiSizingInput probe = *input;
        probe.width = (muiMeasureAxis){0.0f, mui_measureMinContent};
        input->width.size =
            fmaxf(input->width.size, muiSolveNode(solver, root, &probe, false).width);
    }
}

muiSizingInput muiRootInput(const muiLayoutStyle* style, const muiSides* safe, float availableWidth,
                            float availableHeight)
{
    const muiEdges padding = muiPaddingOf(style, safe, muiIsRtl(style, false));
    muiSizingInput input = {
        .width = {availableWidth, mui_measureAtMost},
        .height = {availableHeight, mui_measureAtMost},
        .parentWidth = availableWidth,
        .parentHeight = availableHeight,
    };
    muiAxisSizing width = muiResolveAxis(&style->sizing, true, availableWidth);
    muiAxisSizing height = muiResolveAxis(&style->sizing, false, availableHeight);
    if (width.definite)
    {
        input.width = muiExact(muiClampSize(width.size, width.minimum, width.maximum,
                                            muiBoxSum(&padding, style, true)));
    }
    if (height.definite)
    {
        input.height = muiExact(muiClampSize(height.size, height.minimum, height.maximum,
                                             muiBoxSum(&padding, style, false)));
    }
    return input;
}
