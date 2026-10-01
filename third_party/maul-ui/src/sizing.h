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
        axis.minimum = 0.0f;
    }
    if (!muiResolveDimension(maximum, extent, &axis.maximum))
    {
        axis.maximum = INFINITY;
    }
    return axis;
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

// Whether the start or end margin along an axis is automatic.
static inline bool muiIsMarginAutoStart(const muiLayoutStyle* style, bool horizontal)
{
    return (style->marginAuto & (horizontal ? mui_edgeStart : mui_edgeTop)) != 0;
}

static inline bool muiIsMarginAutoEnd(const muiLayoutStyle* style, bool horizontal)
{
    return (style->marginAuto & (horizontal ? mui_edgeEnd : mui_edgeBottom)) != 0;
}

// A node's margins with its automatic sides as zero.
static inline muiEdges muiMarginsOf(const muiLayoutStyle* style)
{
    muiEdges margins = style->margin;
    muiEdgeMask mask = style->marginAuto;
    margins.start = (mask & mui_edgeStart) != 0 ? 0.0f : margins.start;
    margins.end = (mask & mui_edgeEnd) != 0 ? 0.0f : margins.end;
    margins.top = (mask & mui_edgeTop) != 0 ? 0.0f : margins.top;
    margins.bottom = (mask & mui_edgeBottom) != 0 ? 0.0f : margins.bottom;
    return margins;
}

// Padding and border on one axis.
static inline float muiBoxSum(const muiLayoutStyle* style, bool horizontal)
{
    return muiEdgeSum(&style->padding, horizontal) + muiEdgeSum(&style->border, horizontal);
}

// A border-box size within its limits; the minimum wins over the
// maximum, and padding and border win over both.
static inline float muiClampSize(float size, float minimum, float maximum, float box)
{
    return fmaxf(fmaxf(fminf(size, maximum), minimum), box);
}

#endif // MAUL_UI_SRC_SIZING_H
