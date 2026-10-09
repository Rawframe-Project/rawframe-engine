// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// CSS's absolute positioning inside a flex container, one axis at a time:
// the containing block is the container's padding box; both insets of an
// axis stretch an automatic size between them; otherwise the width is
// fit-content and the height the content's; with no inset the child sits
// where it would as the container's only flex item. The anchor point then
// moves it by a fraction of its own size.

#include "absolute.h"

#include "sizing.h"

#include <math.h>

// The container along one axis: where its padding box and content box
// start in its border box, and how large they are.
typedef struct Span
{
    float paddingStart;
    float paddingSize;
    float contentStart;
    float contentSize;
} Span;

// A child's insets along one axis, resolved against the padding box.
typedef struct Insets
{
    float start;
    float end;
    bool hasStart;
    bool hasEnd;
} Insets;

static Span SpanOf(const muiLayoutStyle* container, const muiEdges* padding, float size,
                   bool horizontal)
{
    float borderStart = muiEdgeStart(&container->border, horizontal);
    float border = muiEdgeSum(&container->border, horizontal);
    return (Span){
        .paddingStart = borderStart,
        .paddingSize = fmaxf(size - border, 0.0f),
        .contentStart = borderStart + muiEdgeStart(padding, horizontal),
        .contentSize = fmaxf(size - muiBoxSum(padding, container, horizontal), 0.0f),
    };
}

// Its insets along an axis, horizontal ones swapped when its own direction
// runs against its container's (muiAgainstParent).
static Insets InsetsOf(const muiInsets* inset, bool horizontal, float extent, bool against)
{
    Insets result = {0};
    muiDimension start = horizontal ? (against ? inset->end : inset->start) : inset->top;
    muiDimension end = horizontal ? (against ? inset->start : inset->end) : inset->bottom;
    result.hasStart = muiResolveDimension(start, extent, &result.start);
    result.hasEnd = muiResolveDimension(end, extent, &result.end);
    return result;
}

// The child's size along an axis when its own value or both insets fix
// it; returns false when it comes from content.
static bool FixedSize(const muiLayoutStyle* style, const muiEdges* padding, bool horizontal,
                      const Span* spanX, const Span* spanY, const Insets* insets, float* sizeOut)
{
    const Span* span = horizontal ? spanX : spanY;
    muiAxisSizing axis = muiResolveAxis(&style->sizing, horizontal, span->paddingSize);
    muiEdges margins = muiMarginsOf(style, false);
    float box = muiBoxSum(padding, style, horizontal);
    if (axis.definite)
    {
        *sizeOut = muiClampSize(axis.size, axis.minimum, axis.maximum, box);
        return true;
    }
    if (insets->hasStart && insets->hasEnd)
    {
        float space =
            span->paddingSize - insets->start - insets->end - muiEdgeSum(&margins, horizontal);
        *sizeOut = muiClampSize(space, axis.minimum, axis.maximum, box);
        return true;
    }
    return false;
}

// How an absolute child with no inset on an axis aligns as the
// container's only flex item, under justify-content on the main axis and
// align-self on the cross axis: the share of the free space before it in
// its flow (0, a half or 1), and whether the flow runs reversed.
static float StaticShare(const muiLayoutStyle* container, const muiLayoutStyle* child,
                         bool horizontal, bool* reverseOut)
{
    muiFlexDirection direction = container->container.direction;
    bool row = direction == mui_flexRow || direction == mui_flexRowReverse;
    if (horizontal == row)
    {
        *reverseOut = direction == mui_flexRowReverse || direction == mui_flexColumnReverse;
        muiJustify justify = container->container.justify;
        bool centered = justify == mui_justifyCenter || justify == mui_justifySpaceAround ||
                        justify == mui_justifySpaceEvenly;
        return justify == mui_justifyEnd ? 1.0f : (centered ? 0.5f : 0.0f);
    }
    muiAlign align = child->item.alignSelf != mui_alignAuto ? child->item.alignSelf
                                                            : container->container.alignItems;
    // Baseline, with no group to share, falls back to the writing mode's
    // start, which wrap-reverse does not flip (as in Chrome).
    *reverseOut = container->container.wrap == mui_wrapReverse && align != mui_alignBaseline;
    return align == mui_alignEnd ? 1.0f : (align == mui_alignCenter ? 0.5f : 0.0f);
}

// Where an absolute child with no inset on an axis sits.
static float StaticOffset(const muiLayoutStyle* container, const muiLayoutStyle* child,
                          bool horizontal, const Span* span, float size, bool rtl)
{
    muiEdges margins = muiMarginsOf(child, rtl);
    float start = muiEdgeStart(&margins, horizontal);
    float end = muiEdgeEnd(&margins, horizontal);
    bool reverse = false;
    float lead = StaticShare(container, child, horizontal, &reverse) *
                 (span->contentSize - size - start - end);
    float flow = lead + (reverse ? end : start);
    float offset = reverse ? span->contentSize - flow - size : flow;
    return span->contentStart + offset;
}

// The room an absolute child with no inset on an axis has: from its
// static position, the container's content box, to the containing
// block's edge it aligns away from; centred, as far each way from the
// content box's centre as the nearer edge (CSS Position 3, section 4.1).
static float StaticRoom(const muiLayoutStyle* container, const muiLayoutStyle* child,
                        bool horizontal, const Span* span)
{
    bool reverse = false;
    float share = StaticShare(container, child, horizontal, &reverse);
    share = reverse ? 1.0f - share : share;
    float before = span->contentStart - span->paddingStart;
    float after = span->paddingSize - before - span->contentSize;
    if (share == 0.5f)
    {
        return 2.0f * fminf(before, after) + span->contentSize;
    }
    return span->paddingSize - (share == 0.0f ? before : after);
}

// The width an absolute child and its margins fit within: what its
// insets leave of the padding box, or with neither inset its static
// room.
static float WidthRoom(const muiLayoutStyle* container, const muiLayoutStyle* child,
                       const Span* spanX, const Insets* insetX)
{
    if (!insetX->hasStart && !insetX->hasEnd)
    {
        return StaticRoom(container, child, true, spanX);
    }
    return spanX->paddingSize - (insetX->hasStart ? insetX->start : 0.0f) -
           (insetX->hasEnd ? insetX->end : 0.0f);
}

// An absolute box's align-self between insets: baseline, with no group to
// share, falls back to start (CSS Box Alignment section 9.3).
static muiAlign InsetAlign(muiAlign align)
{
    return align == mui_alignBaseline ? mui_alignStart : align;
}

// Where a margin box of size outer starts in the padding box, aligned by
// align between both insets (CSS Position 3): crossing insets leave no
// space, at the start inset; a box that overflows the space covers it,
// aligned as far as the bounding box of the space and the padding box
// allows, or starts that box when larger (CSS Box Alignment 4.4.1.2).
static float AlignBetweenInsets(muiAlign align, float padding, const Insets* insets, float outer)
{
    align = InsetAlign(align);
    if (align != mui_alignStart && align != mui_alignEnd && align != mui_alignCenter)
    {
        return insets->start;
    }
    float start = insets->start;
    float end = fmaxf(padding - insets->end, start);
    float free = end - start - outer;
    float place =
        start + (align == mui_alignEnd ? free : (align == mui_alignCenter ? free / 2.0f : 0.0f));
    if (free >= 0.0f)
    {
        return place;
    }
    float limitStart = fminf(start, 0.0f);
    float limitEnd = fmaxf(end, padding);
    if (outer > limitEnd - limitStart)
    {
        return limitStart;
    }
    float low = fmaxf(end - outer, limitStart);
    float high = fminf(start, limitEnd - outer);
    return fminf(fmaxf(place, low), high);
}

// The child's offset from the container's border box along an axis.
static float Offset(const muiLayoutStyle* container, const muiLayoutStyle* child, bool horizontal,
                    const Span* span, const Insets* insets, float size, bool rtl)
{
    muiEdges margins = muiMarginsOf(child, rtl);
    float start = muiEdgeStart(&margins, horizontal);
    float end = muiEdgeEnd(&margins, horizontal);
    if (insets->hasStart && insets->hasEnd)
    {
        // Automatic margins share what the insets leave, none when they
        // cross (as in Chrome), as CSS solves an over-constrained box;
        // without them the end inset gives way.
        float space = fmaxf(span->paddingSize - insets->start - insets->end, 0.0f);
        float freeSpace = space - size - start - end;
        bool autoStart = muiIsMarginAutoStart(child, horizontal, rtl);
        bool autoEnd = muiIsMarginAutoEnd(child, horizontal, rtl);
        if (autoStart && autoEnd && horizontal)
        {
            start += fmaxf(freeSpace, 0.0f) / 2.0f;
        }
        else if (autoStart && autoEnd)
        {
            // Vertically they may be negative (CSS 2.1 section 10.6.4), in
            // the space the insets leave, none when they cross.
            start = (space - size) / 2.0f;
        }
        else if (autoStart)
        {
            start += freeSpace;
        }
        else if (!autoEnd && !horizontal)
        {
            // Vertically its own align-self places it in the space the
            // insets leave; horizontally it starts at its inset.
            // Stretched by its own align-self, it fills the space, or
            // overflows it and start's overflow rule places it, its height
            // automatic or given; automatic alignment starts it at its
            // inset (both as Chrome).
            bool stretched = child->item.alignSelf == mui_alignStretch;
            muiAlign align = stretched ? mui_alignStart : child->item.alignSelf;
            return span->paddingStart + start +
                   AlignBetweenInsets(align, span->paddingSize, insets,
                                      size + muiEdgeSum(&margins, false));
        }
        return span->paddingStart + insets->start + start;
    }
    if (insets->hasStart)
    {
        return span->paddingStart + insets->start + start;
    }
    if (insets->hasEnd)
    {
        return span->paddingStart + span->paddingSize - insets->end - end - size;
    }
    return StaticOffset(container, child, horizontal, span, size, rtl);
}

// With an aspect ratio, a height from both insets gives way to the ratio
// when the width is fixed, and the function returns true; otherwise it
// gives the width, the width's limits, its padding and border among them,
// through the ratio clamping it (CSS Sizing 4, as Chrome). A width from
// both insets is clamped likewise by the height's limits; a height the
// style gives sets the width through the ratio instead of the insets.
static bool RatioTakesHeight(const muiLayoutStyle* style, const muiEdges* padding,
                             const Span* spanX, const Span* spanY, bool* fixedWidth,
                             bool fixedHeight, float* width, float* height)
{
    float ratio = style->sizing.aspectRatio;
    if (ratio <= 0.0f)
    {
        return false;
    }
    muiAxisSizing across = muiResolveAxis(&style->sizing, true, spanX->paddingSize);
    muiAxisSizing down = muiResolveAxis(&style->sizing, false, spanY->paddingSize);
    if (style->item.alignSelf == mui_alignStretch && fixedHeight && !down.definite)
    {
        // Stretched by its own align-self, its height is the insets' as
        // if given, and a width not given follows it through the ratio,
        // past its own insets (as Chrome).
        *fixedWidth = across.definite;
        return false;
    }
    if (down.definite && !across.definite)
    {
        *fixedWidth = false;
        return false;
    }
    if (*fixedWidth && !across.definite)
    {
        float box = muiBoxSum(padding, style, false);
        *width = fminf(fmaxf(*width, fmaxf(down.minimum, box) * ratio),
                       muiAxisCeiling(&down, box) * ratio);
    }
    if (!fixedHeight || down.definite)
    {
        return false;
    }
    float box = muiBoxSum(padding, style, true);
    *height = fminf(fmaxf(*height, fmaxf(across.minimum, box) / ratio),
                    muiAxisCeiling(&across, box) / ratio);
    // Its own maximum holds over what the width carries (as Chrome).
    *height = fminf(*height, muiAxisCeiling(&down, muiBoxSum(padding, style, false)));
    return *fixedWidth;
}

// A height from both insets, with an aspect ratio, is at least the
// width's through the ratio and, its minimum automatic, the content's at
// that width; stretched by its own align-self, it is the insets' as if
// given (both as Chrome).
static float RatioHeightFloor(const muiSolver* solver, uint32_t child, muiSizingInput* input,
                              const Span* spanY, float width, float height)
{
    const muiLayoutStyle* style = &solver->nodes[child - 1].style;
    if (style->sizing.aspectRatio <= 0.0f || style->item.alignSelf == mui_alignStretch ||
        muiResolveAxis(&style->sizing, false, spanY->paddingSize).definite)
    {
        return height;
    }
    input->width = muiExact(width);
    input->height = (muiMeasureAxis){0.0f, mui_measureMaxContent};
    return fmaxf(height, solver->solve(solver, child, input, false).height);
}

// The vertical insets that size a child: between both, start, end or
// centre alignment makes its height fit-content, not stretched (CSS
// Position 3 section 4.1), so the end inset does not size it.
static Insets SizingInsets(const muiLayoutStyle* style, Insets insets)
{
    muiAlign align = InsetAlign(style->item.alignSelf);
    insets.hasEnd = insets.hasEnd && align != mui_alignStart && align != mui_alignEnd &&
                    align != mui_alignCenter;
    return insets;
}

static void PlaceChild(const muiSolver* solver, const muiLayoutStyle* container, uint32_t child,
                       const Span* spanX, const Span* spanY, bool rtl)
{
    muiLayoutNode* layout = &solver->nodes[child - 1];
    const muiLayoutStyle* style = &layout->style;
    bool against = muiAgainstParent(style, rtl);
    Insets insetX = InsetsOf(&style->placement.inset, true, spanX->paddingSize, against);
    Insets insetY = InsetsOf(&style->placement.inset, false, spanY->paddingSize, against);
    if (layout->listed)
    {
        // An item of a virtual list: across it, its content box; along it,
        // its own size, placed after layout (src/virtual.c).
        bool across = container->scrollAxes != mui_scrollHorizontal;
        const Span* span = across ? spanX : spanY;
        Insets* insets = across ? &insetX : &insetY;
        *insets = (Insets){span->contentStart - span->paddingStart,
                           span->paddingStart + span->paddingSize - span->contentStart -
                               span->contentSize,
                           true, true};
    }
    muiSizingInput input = {
        .parentWidth = spanX->paddingSize,
        .parentHeight = spanY->paddingSize,
        .rtl = rtl,
    };
    if (layout->popped)
    {
        // Where its exit popped it, at that size, its height its
        // content's unless its style gives one.
        input.width = muiExact(layout->rect.width);
        input.height = muiExact(layout->rect.height);
        input.contentHeight = !muiResolveAxis(&style->sizing, false, spanY->paddingSize).definite;
        (void)solver->solve(solver, child, &input, true);
        return;
    }
    float width = 0.0f;
    float height = 0.0f;
    // Its padding with the safe area, in the direction it inherits.
    const muiEdges padding = muiPaddingOf(style, &solver->safeArea, muiIsRtl(style, rtl));
    Insets sizingY = SizingInsets(style, insetY);
    bool fixedHeight = FixedSize(style, &padding, false, spanX, spanY, &sizingY, &height);
    bool fixedWidth = FixedSize(style, &padding, true, spanX, spanY, &insetX, &width);
    fixedHeight = !RatioTakesHeight(style, &padding, spanX, spanY, &fixedWidth, fixedHeight, &width,
                                    &height) &&
                  fixedHeight;
    if (!fixedWidth)
    {
        muiEdges margins = muiMarginsOf(style, rtl);
        float room = WidthRoom(container, style, spanX, &insetX);
        float space = room - muiEdgeSum(&margins, true);
        input.width = (muiMeasureAxis){fmaxf(space, 0.0f), mui_measureAtMost};
        input.height =
            fixedHeight ? muiExact(height) : (muiMeasureAxis){0.0f, mui_measureMaxContent};
        width = solver->solve(solver, child, &input, false).width;
    }
    else if (fixedHeight && muiRatioWidthMinimum(style) &&
             muiResolveAxis(&style->sizing, true, spanX->paddingSize).definite)
    {
        // Its ratio may give it a minimum width (src/solve.c).
        input.width = (muiMeasureAxis){0.0f, mui_measureMinContent};
        input.height = muiExact(height);
        width = fmaxf(width, solver->solve(solver, child, &input, false).width);
    }
    height = fixedHeight ? RatioHeightFloor(solver, child, &input, spanY, width, height) : height;
    if (!fixedHeight)
    {
        input.width = muiExact(width);
        input.height = (muiMeasureAxis){0.0f, mui_measureMaxContent};
        height = solver->solve(solver, child, &input, false).height;
    }
    float x = Offset(container, style, true, spanX, &insetX, width, rtl);
    float y = Offset(container, style, false, spanY, &insetY, height, rtl);
    x -= style->placement.anchorX * width;
    y -= style->placement.anchorY * height;
    layout->rect = (muiRect){x, y, width, height};
    input.width = muiExact(width);
    input.height = muiExact(height);
    // A height neither given nor set by both insets is its content's,
    // unless its aspect ratio gives it.
    input.contentHeight = !fixedHeight && style->sizing.aspectRatio <= 0.0f;
    (void)solver->solve(solver, child, &input, true);
}

void muiPlaceAbsolute(const muiSolver* solver, uint32_t container, muiSize size, bool rtl)
{
    const muiLayoutStyle* style = &solver->nodes[container - 1].style;
    const muiEdges padding = muiPaddingOf(style, &solver->safeArea, rtl);
    Span spanX = SpanOf(style, &padding, size.width, true);
    Span spanY = SpanOf(style, &padding, size.height, false);
    for (uint32_t c = muiTreeAt(solver->tree, container)->links.firstChild; c != 0;
         c = muiTreeAt(solver->tree, c)->links.next)
    {
        muiLayoutNode* layout = &solver->nodes[c - 1];
        if (layout->absolute)
        {
            PlaceChild(solver, style, c, &spanX, &spanY, rtl);
        }
    }
}
