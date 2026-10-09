// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The cache answers a sizing query when a stored result provably answers
// it the same; everything else is computed and stored.

#include "solve.h"

#include "absolute.h"
#include "condition.h"
#include "flex.h"
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
    if (style->content == mui_contentHost && solver->measure != nullptr && !decided)
    {
        muiNodeId id = muiTreeIdOf(solver->tree, node);
        uint64_t hostKey = muiTreeAt(solver->tree, node)->hostKey;
        solver->work->measured++;
        content =
            solver->measure(solver->measureUser, id, hostKey, ContentAxis(input->width, boxWidth),
                            ContentAxis(input->height, boxHeight));
        content.width = SaneLength(content.width);
        content.height = SaneLength(content.height);
    }
    muiAxisSizing width = muiResolveAxis(&style->sizing, true, input->parentWidth);
    muiAxisSizing height = muiResolveAxis(&style->sizing, false, input->parentHeight);
    if (input->contentOnly)
    {
        width = height = (muiAxisSizing){.maximum = INFINITY};
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

// A scroll container's extent, while its children are still in logical
// coordinates: the furthest end of their margin boxes (border boxes for
// absolute ones) plus its end padding, from its padding box's start, at
// least the padding box; its offsets are brought within it, rtl its own
// direction. What changes an extent repaints the container, and so its
// transform.
static void MeasureExtent(const muiSolver* solver, uint32_t node, muiSize size, bool rtl)
{
    const muiLayoutStyle* style = &solver->nodes[node - 1].style;
    const muiEdges* padding = &solver->paddings[node - 1];
    float startX = style->border.start;
    float startY = style->border.top;
    float reachX = size.width - style->border.end - padding->end;
    float reachY = size.height - style->border.bottom - padding->bottom;
    for (uint32_t c = muiTreeAt(solver->tree, node)->links.firstChild; c != 0;
         c = muiTreeAt(solver->tree, c)->links.next)
    {
        const muiLayoutNode* child = &solver->nodes[c - 1];
        if (child->listed)
        {
            // Placed along its list after layout: the list's length below
            // reaches past it, and across it the content box holds it.
            continue;
        }
        muiEdges margins = child->absolute ? (muiEdges){0} : muiMarginsOf(&child->style, rtl);
        // A popped child is where it was drawn, mirrored under rtl.
        float x =
            child->popped && rtl ? size.width - child->rect.x - child->rect.width : child->rect.x;
        reachX = fmaxf(reachX, x + child->rect.width + margins.end);
        reachY = fmaxf(reachY, child->rect.y + child->rect.height + margins.bottom);
    }
    muiScrollState* scroll = &solver->scrolls[node - 1];
    scroll->reachWidth = reachX + padding->end - startX;
    scroll->reachHeight = reachY + padding->bottom - startY;
    // A virtual list's items reach as far as they need, realized or not.
    scroll->extentWidth = fmaxf(scroll->reachWidth, padding->start + scroll->listX + padding->end);
    scroll->extentHeight =
        fmaxf(scroll->reachHeight, padding->top + scroll->listY + padding->bottom);
    // At the size given here: its parent sets its rectangle after this.
    scroll->x = fminf(scroll->x, muiScrollLimit(style, size, scroll, true));
    scroll->y = fminf(scroll->y, muiScrollLimit(style, size, scroll, false));
}

// Lays a container out in logical coordinates, start on the left, and
// mirrors it when its direction is right to left (record mui-0003).
static muiSize SizeContainer(const muiSolver* solver, uint32_t node, const muiSizingInput* input,
                             bool perform)
{
    muiSize size = muiLayoutFlex(solver, node, input, perform);
    if (perform)
    {
        muiPlaceAbsolute(solver, node, size, input->rtl);
        if (solver->nodes[node - 1].style.scrollAxes != mui_scrollNone)
        {
            MeasureExtent(solver, node, size, input->rtl);
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
        return ContentSize(solver, node, input, false);
    }
    // A row laid out at the ratio's height, definite for its items.
    muiSizingInput probe = *input;
    probe.height = muiExact(size);
    probe.contentHeight = false;
    return muiFlexRatioContentHeight(solver, node, &probe);
}

// The size the aspect ratio gives an axis from the other one's, at least
// the content's min-content size when the axis's minimum is automatic
// (CSS Sizing 4). A width given as a percentage that cannot resolve takes that
// minimum too; a height so given does not, as in Chrome. Within its
// limits.
static muiMeasureAxis RatioAxis(const muiSolver* solver, uint32_t node, const muiSizingInput* input,
                                bool horizontal, float size)
{
    const muiLayoutStyle* style = &solver->nodes[node - 1].style;
    muiAxisSizing axis = muiResolveAxis(&style->sizing, horizontal,
                                        horizontal ? input->parentWidth : input->parentHeight);
    if (axis.minimumAuto && (horizontal || style->sizing.height.kind == mui_dimensionAuto))
    {
        // A row's stretched items take a height from the ratio, so they
        // leave its floor; asked its min-content height, as a column
        // measures its items, the node counts its content in full.
        bool full = horizontal || input->height.mode == mui_measureMinContent;
        float floor = full ? ContentSize(solver, node, input, horizontal)
                           : RatioContentHeight(solver, node, input, size);
        muiAxisSizing height = muiResolveAxis(&style->sizing, false, input->parentHeight);
        if (horizontal && !height.definite)
        {
            // A width's floor is capped by the height's maximum through
            // the ratio, a height not given (as Chrome; a height's floor
            // is not capped by the width's).
            float box = muiBoxSum(&solver->paddings[node - 1], style, false);
            floor = fminf(floor, muiAxisCeiling(&height, box) * style->sizing.aspectRatio);
        }
        size = fmaxf(size, floor);
    }
    return muiExact(muiClampSize(size, axis.minimum, axis.maximum,
                                 muiBoxSum(&solver->paddings[node - 1], style, horizontal)));
}

// With an aspect ratio, a node given an exact size on one axis and an
// automatic one on the other takes the other from the ratio. A root or
// absolute node given both, with a minimum width from its ratio, answers
// a query for its min-content width with at least its content's.
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
    else if (!exactWidth && !exactHeight && !width.definite && height.definite &&
             input->height.mode != mui_measureMinContent)
    {
        // Its style gives the height, the ratio the width, whatever the
        // query leaves open, as a column asks its items' widths; asked
        // its min-content height, as for a content size suggestion, its
        // content answers.
        const muiLayoutStyle* style = &solver->nodes[node - 1].style;
        input->height =
            muiExact(muiClampSize(height.size, height.minimum, height.maximum,
                                  muiBoxSum(&solver->paddings[node - 1], style, false)));
        input->width = RatioAxis(solver, node, input, true, input->height.size * ratio);
    }
    else if (exactHeight && input->width.mode == mui_measureMinContent && width.definite &&
             muiRatioWidthMinimum(&solver->nodes[node - 1].style) &&
             (solver->nodes[node - 1].absolute || muiTreeAt(solver->tree, node)->links.parent == 0))
    {
        float content = fminf(ContentSize(solver, node, input, true), width.maximum);
        const muiLayoutStyle* style = &solver->nodes[node - 1].style;
        float own = muiClampSize(width.size, width.minimum, width.maximum,
                                 muiBoxSum(&solver->paddings[node - 1], style, true));
        input->width = muiExact(fmaxf(own, content));
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
// read a size or direction this layout changed, requests its style for
// the next pass: conditions read the state before the pass (record
// mui-0004). A held node is styled again only when its size leaves the
// two it oscillated between.
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
            MeasureExtent(solver, node, size, own.rtl);
        }
        cache->finalValid = true;
        cache->finalRtl = input->rtl;
        cache->finalContentHeight = input->contentHeight;
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
    return top + solver->measureBaseline(solver->measureUser, id, hostKey, width, height);
}

float muiSolveBaseline(const muiSolver* solver, uint32_t node, const muiSizingInput* input)
{
    MUI_ASSERT(input->width.mode == mui_measureExact && input->height.mode == mui_measureExact);
    muiSizingInput own = OwnDirection(&solver->nodes[node - 1].style, input);
    float baseline = muiTreeAt(solver->tree, node)->links.firstChild == 0
                         ? LeafBaseline(solver, node, &own)
                         : muiFlexBaseline(solver, node, &own);
    return isfinite(baseline) ? baseline : input->height.size;
}

void muiRatioRootWidth(const muiSolver* solver, uint32_t root, muiSizingInput* input)
{
    if (muiRatioWidthMinimum(&solver->nodes[root - 1].style) &&
        input->width.mode == mui_measureExact && input->height.mode == mui_measureExact)
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
