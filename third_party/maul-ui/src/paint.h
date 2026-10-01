// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Painting one node into a list's tables: its shadows, box and image,
// snapped per kind, and the clip it gives its children (record mui-0005).

#ifndef MAUL_UI_SRC_PAINT_H
#define MAUL_UI_SRC_PAINT_H

#include "context.h"
#include "draw_store.h"

#include <stdbool.h>
#include <stdint.h>

enum
{
    // Colors converted in a build, kept by a hash of their bits: nodes
    // share colors through their classes, and a conversion takes three
    // powers.
    MUI_PAINT_COLOR_CACHE = 64
};

// A color's bits and its red, green and blue in linear light. A zeroed
// entry holds clear black, whose are 0.
typedef struct muiCachedColor
{
    uint32_t bits[4];
    double rgb[3];
} muiCachedColor;

// What one build writes to, the host's paint function, whether something
// did not fit, and how many of the host's calls were refused.
typedef struct muiPainter
{
    const muiContext* context;
    muiDrawTables* out;
    uint32_t commandCapacity;
    uint32_t clipCapacity;
    uint32_t gradientCapacity;
    uint32_t glyphCapacity;
    float scale;
    bool full;
    muiPaintFunction paint;
    void* paintUser;
    uint64_t misuse;
    muiCachedColor colors[MUI_PAINT_COLOR_CACHE];
} muiPainter;

// A color in linear light, premultiplied, times opacity.
muiLinearColor muiPaintColor(muiPainter* painter, muiColor color, float opacity);

// An edge at the nearest device pixel.
float muiSnapEdge(float value, float scale);

// A zeroed command of the kind in the clip, or NULL, marking the painter
// full, when the list has no room.
muiDrawCommand* muiTakeCommand(muiPainter* painter, muiDrawKind kind, uint32_t clip);

// Paints a node's own commands at the origin, clip and inherited opacity
// in state, and leaves in state the clip and opacity its children are
// painted in. False when the node, and so its subtree, draws nothing.
bool muiPaintNode(muiPainter* painter, uint32_t slot, muiPaintState* state);

#endif // MAUL_UI_SRC_PAINT_H
