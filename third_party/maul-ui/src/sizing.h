// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Reading a node's authored values along one axis: Scale+Offset
// dimensions, sides of boxes, margins with their automatic sides, and
// limits. Inline, as the solver calls them in its innermost loops.

#ifndef MAUL_UI_SRC_SIZING_H
#define MAUL_UI_SRC_SIZING_H

#include "maul-ui/layout.h"

#include <math.h>
#include <stdbool.h>

// One axis's size and limits of a node.
typedef struct muiAxisSizing
{
    float size;
    float minimum;
    float maximum;
    bool definite;
    bool minimumAuto;
} muiAxisSizing;

// Resolves a dimension against an extent, negative when indefinite;
// returns false for an automatic value or a scale against an indefinite
// extent.
static inline bool muiResolveDimension(muiDimension dimension, float extent, float* valueOut)
{
    if (dimension.kind != mui_dimensionValue)
    {
        return false;
    }
    if (dimension.scale == 0.0f)
    {
        *valueOut = dimension.offset;
        return true;
    }
    if (extent < 0.0f)
    {
        return false;
    }
    *valueOut = dimension.scale * extent + dimension.offset;
    return true;
}

// A node's size and limits on one axis, against its parent's extent.
static inline muiAxisSizing muiResolveAxis(const muiSizing* sizing, bool horizontal, float extent)
{
    muiAxisSizing axis = {.maximum = INFINITY};
    muiDimension size = horizontal ? sizing->width : sizing->height;
    muiDimension minimum = horizontal ? sizing->minWidth : sizing->minHeight;
    muiDimension maximum = horizontal ? sizing->maxWidth : sizing->maxHeight;
    axis.definite = muiResolveDimension(size, extent, &axis.size);
    axis.minimumAuto = minimum.kind == mui_dimensionAuto;
    if (!muiResolveDimension(minimum, extent, &axis.minimum))
    {
        // A scale against an indefinite extent resolves against 0, its
        // offset kept (CSS Sizing 3 section 5.2.1), as in Chrome.
        axis.minimum = minimum.kind == mui_dimensionValue ? fmaxf(minimum.offset, 0.0f) : 0.0f;
    }
    if (!muiResolveDimension(maximum, extent, &axis.maximum))
    {
        axis.maximum = INFINITY;
    }
    return axis;
}

// An axis's maximum as it holds: under its minimum and its box, which
// win over it (CSS 2.1 section 10.4), for carrying through a ratio.
static inline float muiAxisCeiling(const muiAxisSizing* axis, float box)
{
    return fmaxf(fmaxf(axis->maximum, axis->minimum), box);
}

static inline muiMeasureAxis muiExact(float size)
{
    return (muiMeasureAxis){size, mui_measureExact};
}

// The start or end side of edges along an axis (start and end
// horizontally, top and bottom vertically), and both together.
static inline float muiEdgeStart(const muiEdges* edges, bool horizontal)
{
    return horizontal ? edges->start : edges->top;
}

static inline float muiEdgeEnd(const muiEdges* edges, bool horizontal)
{
    return horizontal ? edges->end : edges->bottom;
}

// The start or end side of edges along an axis (start and end
// horizontally, top and bottom vertically), and both together.
static inline float muiEdgeSum(const muiEdges* edges, bool horizontal)
{
    return muiEdgeStart(edges, horizontal) + muiEdgeEnd(edges, horizontal);
}

// Whether a node's own direction runs against its parent's, which lays
// it out: its logical margins and insets then map to the other physical
// sides, as CSS Logical maps a box's properties by its own direction.
static inline bool muiAgainstParent(const muiLayoutStyle* style, bool parentRtl)
{
    return style->textDirection != mui_textInherit &&
           (style->textDirection == mui_textRightToLeft) != parentRtl;
}

// Whether the start or end margin along an axis is automatic, in the
// parent's direction.
static inline bool muiIsMarginAutoStart(const muiLayoutStyle* style, bool horizontal,
                                        bool parentRtl)
{
    muiEdgeMask start = muiAgainstParent(style, parentRtl) ? mui_edgeEnd : mui_edgeStart;
    return (style->marginAuto & (horizontal ? start : mui_edgeTop)) != 0;
}

static inline bool muiIsMarginAutoEnd(const muiLayoutStyle* style, bool horizontal, bool parentRtl)
{
    muiEdgeMask end = muiAgainstParent(style, parentRtl) ? mui_edgeStart : mui_edgeEnd;
    return (style->marginAuto & (horizontal ? end : mui_edgeBottom)) != 0;
}

// A node's margins with its automatic sides as zero, in its parent's
// direction.
static inline muiEdges muiMarginsOf(const muiLayoutStyle* style, bool parentRtl)
{
    muiEdges margins = style->margin;
    muiEdgeMask mask = style->marginAuto;
    margins.start = (mask & mui_edgeStart) != 0 ? 0.0f : margins.start;
    margins.end = (mask & mui_edgeEnd) != 0 ? 0.0f : margins.end;
    margins.top = (mask & mui_edgeTop) != 0 ? 0.0f : margins.top;
    margins.bottom = (mask & mui_edgeBottom) != 0 ? 0.0f : margins.bottom;
    if (muiAgainstParent(style, parentRtl))
    {
        float start = margins.start;
        margins.start = margins.end;
        margins.end = start;
    }
    return margins;
}

// Whether a node lays out right to left: its own direction, or the one
// it inherits.
static inline bool muiIsRtl(const muiLayoutStyle* style, bool inherited)
{
    return style->textDirection == mui_textInherit ? inherited
                                                   : style->textDirection == mui_textRightToLeft;
}

// A node's padding with the safe area it asks for: on each edge its mask
// names, at least the inset on the physical side that edge falls on in
// its direction (record mui-0003).
static inline muiEdges muiPaddingOf(const muiLayoutStyle* style, const muiSides* safe, bool rtl)
{
    muiEdges padding = style->padding;
    muiEdgeMask mask = style->safeArea;
    if (mask == 0)
    {
        return padding;
    }
    if ((mask & mui_edgeStart) != 0)
    {
        padding.start = fmaxf(padding.start, rtl ? safe->right : safe->left);
    }
    if ((mask & mui_edgeEnd) != 0)
    {
        padding.end = fmaxf(padding.end, rtl ? safe->left : safe->right);
    }
    if ((mask & mui_edgeTop) != 0)
    {
        padding.top = fmaxf(padding.top, safe->top);
    }
    if ((mask & mui_edgeBottom) != 0)
    {
        padding.bottom = fmaxf(padding.bottom, safe->bottom);
    }
    return padding;
}

// Padding and border on one axis.
static inline float muiBoxSum(const muiEdges* padding, const muiLayoutStyle* style, bool horizontal)
{
    return muiEdgeSum(padding, horizontal) + muiEdgeSum(&style->border, horizontal);
}

// A border-box size within its limits; the minimum wins over the
// maximum, and padding and border win over both.
static inline float muiClampSize(float size, float minimum, float maximum, float box)
{
    return fmaxf(fmaxf(fminf(size, maximum), minimum), box);
}

// Whether a box given both sizes keeps its content's min-content width:
// with an aspect ratio, Chrome takes the width as the ratio-dependent
// axis once the height is not automatic, and CSS Sizing 4 section 4.3
// gives that axis the content's min-content size as an automatic
// minimum. A box scrolling across has none.
static inline bool muiRatioWidthMinimum(const muiLayoutStyle* style)
{
    return style->sizing.aspectRatio > 0.0f && style->sizing.minWidth.kind == mui_dimensionAuto &&
           style->sizing.height.kind != mui_dimensionAuto &&
           (style->scrollAxes & mui_scrollHorizontal) == 0;
}

#endif // MAUL_UI_SRC_SIZING_H
