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

static Span SpanOf(const muiLayoutStyle* container, float size, bool horizontal)
{
    float borderStart = muiEdgeStart(&container->border, horizontal);
    float border = muiEdgeSum(&container->border, horizontal);
    return (Span){
        .paddingStart = borderStart,
        .paddingSize = fmaxf(size - border, 0.0f),
        .contentStart = borderStart + muiEdgeStart(&container->padding, horizontal),
        .contentSize = fmaxf(size - muiBoxSum(container, horizontal), 0.0f),
    };
}

static Insets InsetsOf(const muiInsets* inset, bool horizontal, float extent)
{
    Insets result = {0};
    result.hasStart =
        muiResolveDimension(horizontal ? inset->start : inset->top, extent, &result.start);
    result.hasEnd =
        muiResolveDimension(horizontal ? inset->end : inset->bottom, extent, &result.end);
    return result;
}

// The child's size along an axis when its own value or both insets fix
// it; returns false when it comes from content.
static bool FixedSize(const muiLayoutStyle* style, bool horizontal, const Span* spanX,
                      const Span* spanY, const Insets* insets, float* sizeOut)
{
    const Span* span = horizontal ? spanX : spanY;
    muiAxisSizing axis = muiResolveAxis(&style->sizing, horizontal, span->paddingSize);
    muiEdges margins = muiMarginsOf(style);
    float box = muiBoxSum(style, horizontal);
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

// Where an absolute child with no inset on an axis sits: as the
// container's only flex item, under justify-content on the main axis and
// align-self on the cross axis.
static float StaticOffset(const muiLayoutStyle* container, const muiLayoutStyle* child,
                          bool horizontal, const Span* span, float size)
{
    muiFlexDirection direction = container->container.direction;
    bool row = direction == mui_flexRow || direction == mui_flexRowReverse;
    muiEdges margins = muiMarginsOf(child);
    float start = muiEdgeStart(&margins, horizontal);
    float end = muiEdgeEnd(&margins, horizontal);
    float freeSpace = span->contentSize - size - start - end;
    bool reverse = false;
    float lead = 0.0f;
    if (horizontal == row)
    {
        reverse = direction == mui_flexRowReverse || direction == mui_flexColumnReverse;
        muiJustify justify = container->container.justify;
        bool centered = justify == mui_justifyCenter || justify == mui_justifySpaceAround ||
                        justify == mui_justifySpaceEvenly;
        lead = justify == mui_justifyEnd ? freeSpace : (centered ? freeSpace / 2.0f : 0.0f);
    }
    else
    {
        reverse = container->container.wrap == mui_wrapReverse;
        muiAlign align = child->item.alignSelf != mui_alignAuto ? child->item.alignSelf
                                                                : container->container.alignItems;
        lead = align == mui_alignEnd ? freeSpace
                                     : (align == mui_alignCenter ? freeSpace / 2.0f : 0.0f);
    }
    float flow = lead + (reverse ? end : start);
    float offset = reverse ? span->contentSize - flow - size : flow;
    return span->contentStart + offset;
}

// The child's offset from the container's border box along an axis.
static float Offset(const muiLayoutStyle* container, const muiLayoutStyle* child, bool horizontal,
                    const Span* span, const Insets* insets, float size)
{
    muiEdges margins = muiMarginsOf(child);
    float start = muiEdgeStart(&margins, horizontal);
    float end = muiEdgeEnd(&margins, horizontal);
    if (insets->hasStart && insets->hasEnd)
    {
        // Automatic margins share what the insets leave, as CSS solves an
        // over-constrained box; without them the end inset gives way.
        float freeSpace = span->paddingSize - insets->start - insets->end - size - start - end;
        bool autoStart = muiIsMarginAutoStart(child, horizontal);
        bool autoEnd = muiIsMarginAutoEnd(child, horizontal);
        if (autoStart && autoEnd)
        {
            start += fmaxf(freeSpace, 0.0f) / 2.0f;
        }
        else if (autoStart)
        {
            start += freeSpace;
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
    return StaticOffset(container, child, horizontal, span, size);
}

static void PlaceChild(const muiSolver* solver, const muiLayoutStyle* container, uint32_t child,
                       const Span* spanX, const Span* spanY, bool rtl)
{
    muiLayoutNode* layout = &solver->nodes[child - 1];
    const muiLayoutStyle* style = &layout->style;
    Insets insetX = InsetsOf(&style->placement.inset, true, spanX->paddingSize);
    Insets insetY = InsetsOf(&style->placement.inset, false, spanY->paddingSize);
    muiSizingInput input = {
        .parentWidth = spanX->paddingSize,
        .parentHeight = spanY->paddingSize,
        .rtl = rtl,
    };
    float width = 0.0f;
    float height = 0.0f;
    bool fixedHeight = FixedSize(style, false, spanX, spanY, &insetY, &height);
    if (!FixedSize(style, true, spanX, spanY, &insetX, &width))
    {
        muiEdges margins = muiMarginsOf(style);
        float space = spanX->paddingSize - (insetX.hasStart ? insetX.start : 0.0f) -
                      (insetX.hasEnd ? insetX.end : 0.0f) - muiEdgeSum(&margins, true);
        input.width = (muiMeasureAxis){fmaxf(space, 0.0f), mui_measureAtMost};
        input.height =
            fixedHeight ? muiExact(height) : (muiMeasureAxis){0.0f, mui_measureMaxContent};
        width = solver->solve(solver, child, &input, false).width;
    }
    if (!fixedHeight)
    {
        input.width = muiExact(width);
        input.height = (muiMeasureAxis){0.0f, mui_measureMaxContent};
        height = solver->solve(solver, child, &input, false).height;
    }
    float x = Offset(container, style, true, spanX, &insetX, width);
    float y = Offset(container, style, false, spanY, &insetY, height);
    x -= style->placement.anchorX * width;
    y -= style->placement.anchorY * height;
    layout->rect = (muiRect){x, y, width, height};
    input.width = muiExact(width);
    input.height = muiExact(height);
    (void)solver->solve(solver, child, &input, true);
}

void muiPlaceAbsolute(const muiSolver* solver, uint32_t container, muiSize size, bool rtl)
{
    const muiLayoutStyle* style = &solver->nodes[container - 1].style;
    Span spanX = SpanOf(style, size.width, true);
    Span spanY = SpanOf(style, size.height, false);
    for (uint32_t c = muiTreeAt(solver->tree, container)->links.firstChild; c != 0;
         c = muiTreeAt(solver->tree, c)->links.next)
    {
        if (solver->nodes[c - 1].absolute)
        {
            PlaceChild(solver, style, c, &spanX, &spanY, rtl);
        }
    }
}
