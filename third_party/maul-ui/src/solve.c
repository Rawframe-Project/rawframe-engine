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
// space; or a smaller space the old size still fits.
static bool AxisAnswers(muiMeasureAxis next, muiMeasureAxis old, float result)
{
    if (next.mode == old.mode && (!IsSized(next.mode) || next.size == old.size))
    {
        return true;
    }
    if (next.mode == mui_measureExact)
    {
        return old.mode != mui_measureMinContent && next.size == result;
    }
    if (next.mode == mui_measureAtMost)
    {
        bool fitsMaxContent = old.mode == mui_measureMaxContent;
        bool stricter = old.mode == mui_measureAtMost && next.size < old.size;
        return (fitsMaxContent || stricter) && result <= next.size;
    }
    return false;
}

static bool IsScaled(muiDimension dimension)
{
    return dimension.kind == mui_dimensionValue && dimension.scale != 0.0f;
}

// Whether a node's own sizing reads its parent's extents: only its scaled
// limits do, as the parent resolves the node's size itself. Direction is
// not part of the key: it moves children, never changes a size.
static bool ReadsParentExtent(const muiSizing* sizing)
{
    return IsScaled(sizing->minWidth) || IsScaled(sizing->maxWidth) ||
           IsScaled(sizing->minHeight) || IsScaled(sizing->maxHeight);
}

static const muiCacheEntry* FindCached(const muiLayoutCache* cache, const muiSizingInput* input,
                                       bool keyExtents)
{
    for (int i = 0; i < MUI_CACHE_ENTRIES; i++)
    {
        const muiCacheEntry* entry = &cache->entries[i];
        bool sameExtents = !keyExtents || (entry->input.parentWidth == input->parentWidth &&
                                           entry->input.parentHeight == input->parentHeight);
        if (entry->valid && sameExtents &&
            AxisAnswers(input->width, entry->input.width, entry->size.width) &&
            AxisAnswers(input->height, entry->input.height, entry->size.height))
        {
            return entry;
        }
    }
    return nullptr;
}

static void StoreCached(muiLayoutCache* cache, const muiSizingInput* input, muiSize size)
{
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
    float boxWidth = muiBoxSum(style, true);
    float boxHeight = muiBoxSum(style, false);
    muiSize content = {0.0f, 0.0f};
    // Both sizes exact decide the size, as the final pass always gives
    // them: the host is not asked.
    bool decided = input->width.mode == mui_measureExact && input->height.mode == mui_measureExact;
    if (style->content == mui_contentHost && solver->measure != nullptr && !decided)
    {
        muiNodeId id = muiTreeIdOf(solver->tree, node);
        uint64_t hostKey = muiTreeAt(solver->tree, node)->hostKey;
        content =
            solver->measure(solver->measureUser, id, hostKey, ContentAxis(input->width, boxWidth),
                            ContentAxis(input->height, boxHeight));
        content.width = SaneLength(content.width);
        content.height = SaneLength(content.height);
    }
    muiAxisSizing width = muiResolveAxis(&style->sizing, true, input->parentWidth);
    muiAxisSizing height = muiResolveAxis(&style->sizing, false, input->parentHeight);
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
        muiRect* rect = &solver->nodes[c - 1].rect;
        rect->x = width - rect->x - rect->width;
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
// least the padding box; its offsets are brought within it. What changes
// an extent repaints the container, and so its transform.
static void MeasureExtent(const muiSolver* solver, uint32_t node, muiSize size)
{
    const muiLayoutStyle* style = &solver->nodes[node - 1].style;
    float startX = style->border.start;
    float startY = style->border.top;
    float reachX = size.width - style->border.end - style->padding.end;
    float reachY = size.height - style->border.bottom - style->padding.bottom;
    for (uint32_t c = muiTreeAt(solver->tree, node)->links.firstChild; c != 0;
         c = muiTreeAt(solver->tree, c)->links.next)
    {
        const muiLayoutNode* child = &solver->nodes[c - 1];
        muiEdges margins = child->absolute ? (muiEdges){0} : muiMarginsOf(&child->style);
        reachX = fmaxf(reachX, child->rect.x + child->rect.width + margins.end);
        reachY = fmaxf(reachY, child->rect.y + child->rect.height + margins.bottom);
    }
    muiScrollState* scroll = &solver->scrolls[node - 1];
    // A virtual list's items reach as far as they need, realized or not.
    reachX = fmaxf(reachX, startX + style->padding.start + scroll->listX);
    reachY = fmaxf(reachY, startY + style->padding.top + scroll->listY);
    scroll->extentWidth = reachX + style->padding.end - startX;
    scroll->extentHeight = reachY + style->padding.bottom - startY;
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
            MeasureExtent(solver, node, size);
        }
        if (input->rtl)
        {
            Mirror(solver, node, size.width);
        }
        MarkMoved(solver, node);
    }
    return size;
}

// The node's min-content size along an axis, the other axis as input
// gives it, computed without its aspect ratio.
static float ContentSize(const muiSolver* solver, uint32_t node, const muiSizingInput* input,
                         bool horizontal)
{
    muiSizingInput probe = *input;
    muiMeasureAxis* axis = horizontal ? &probe.width : &probe.height;
    *axis = (muiMeasureAxis){0.0f, mui_measureMinContent};
    muiSize size = muiTreeAt(solver->tree, node)->links.firstChild == 0
                       ? SizeLeaf(solver, node, &probe)
                       : muiLayoutFlex(solver, node, &probe, false);
    return horizontal ? size.width : size.height;
}

// The size the aspect ratio gives an axis from the other one's, at least
// the content's min-content size when the axis's minimum is automatic (CSS
// Sizing 4), within its limits.
static muiMeasureAxis RatioAxis(const muiSolver* solver, uint32_t node, const muiSizingInput* input,
                                bool horizontal, float size)
{
    const muiLayoutStyle* style = &solver->nodes[node - 1].style;
    muiAxisSizing axis = muiResolveAxis(&style->sizing, horizontal,
                                        horizontal ? input->parentWidth : input->parentHeight);
    if (axis.minimumAuto)
    {
        size = fmaxf(size, ContentSize(solver, node, input, horizontal));
    }
    return muiExact(muiClampSize(size, axis.minimum, axis.maximum, muiBoxSum(style, horizontal)));
}

// With an aspect ratio, a node given an exact size on one axis and an
// automatic one on the other takes the other from the ratio.
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
}

// The input with the node's own direction in place of the inherited one.
static muiSizingInput OwnDirection(const muiLayoutStyle* style, const muiSizingInput* input)
{
    muiSizingInput own = *input;
    if (style->textDirection != mui_textInherit)
    {
        own.rtl = style->textDirection == mui_textRightToLeft;
    }
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
        const muiCacheEntry* hit =
            FindCached(cache, input, ReadsParentExtent(&solver->nodes[node - 1].style.sizing));
        if (hit != nullptr)
        {
            return hit->size;
        }
    }
    muiSizingInput own = OwnDirection(&solver->nodes[node - 1].style, input);
    ApplyAspectRatio(solver, node, &own);
    muiSize size = muiTreeAt(solver->tree, node)->links.firstChild == 0
                       ? SizeLeaf(solver, node, &own)
                       : SizeContainer(solver, node, &own, perform);
    if (perform)
    {
        cache->finalValid = true;
        cache->finalRtl = input->rtl;
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
    float top = muiEdgeStart(&style->padding, false) + muiEdgeStart(&style->border, false);
    float width = fmaxf(input->width.size - muiBoxSum(style, true), 0.0f);
    float height = fmaxf(input->height.size - muiBoxSum(style, false), 0.0f);
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

muiSizingInput muiRootInput(const muiLayoutStyle* style, float availableWidth,
                            float availableHeight)
{
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
        input.width = muiExact(
            muiClampSize(width.size, width.minimum, width.maximum, muiBoxSum(style, true)));
    }
    if (height.definite)
    {
        input.height = muiExact(
            muiClampSize(height.size, height.minimum, height.maximum, muiBoxSum(style, false)));
    }
    return input;
}
