// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Skyline bottom-left packing (Jylänki 2010, as stb_rect_pack and Skia's
// plots do it).

#include "skyline.h"

#include <string.h>

void muiSkylineReset(muiSkyline* skyline)
{
    skyline->nodes[0] = (muiSkylineNode){0, 0, skyline->width};
    skyline->count = 1;
}

// The lowest a rectangle width wide can sit with its left on node
// first's: the deepest segment it spans; UINT32_MAX when it runs past
// the right edge.
static uint32_t FitAt(const muiSkyline* skyline, uint32_t first, uint32_t width)
{
    const muiSkylineNode* nodes = skyline->nodes;
    if (nodes[first].x + width > skyline->width)
    {
        return UINT32_MAX;
    }
    uint32_t y = 0;
    uint32_t covered = 0;
    for (uint32_t i = first; covered < width; i++)
    {
        y = nodes[i].y > y ? nodes[i].y : y;
        covered += nodes[i].width;
    }
    return y;
}

bool muiSkylineInsert(muiSkyline* skyline, uint32_t width, uint32_t height, uint32_t* xOut,
                      uint32_t* yOut)
{
    if (width == 0 || height == 0 || width > skyline->width || height > skyline->height)
    {
        return false;
    }
    uint32_t best = UINT32_MAX;
    uint32_t bestY = UINT32_MAX;
    for (uint32_t i = 0; i < skyline->count; i++)
    {
        uint32_t y = FitAt(skyline, i, width);
        if (y < bestY && y + height <= skyline->height)
        {
            best = i;
            bestY = y;
        }
    }
    if (best == UINT32_MAX)
    {
        return false;
    }
    muiSkylineNode* nodes = skyline->nodes;
    uint32_t x = nodes[best].x;
    uint32_t right = x + width;
    // The segments the rectangle covers are cut to what is right of it;
    // the first that keeps any width stays, behind the new one.
    uint32_t next = best;
    while (next < skyline->count && nodes[next].x + nodes[next].width <= right)
    {
        next++;
    }
    if (next < skyline->count && nodes[next].x < right)
    {
        nodes[next].width = (uint16_t)(nodes[next].x + nodes[next].width - right);
        nodes[next].x = (uint16_t)right;
    }
    // Nodes best up to next are replaced by the one new segment.
    uint32_t removed = next - best;
    memmove(&nodes[best + 1], &nodes[next], (skyline->count - next) * sizeof(muiSkylineNode));
    skyline->count = skyline->count - removed + 1;
    nodes[best] = (muiSkylineNode){(uint16_t)x, (uint16_t)(bestY + height), (uint16_t)width};
    // Neighbors at the same depth become one segment.
    if (best + 1 < skyline->count && nodes[best + 1].y == nodes[best].y)
    {
        nodes[best].width = (uint16_t)(nodes[best].width + nodes[best + 1].width);
        memmove(&nodes[best + 1], &nodes[best + 2],
                (skyline->count - best - 2) * sizeof(muiSkylineNode));
        skyline->count--;
    }
    if (best > 0 && nodes[best - 1].y == nodes[best].y)
    {
        nodes[best - 1].width = (uint16_t)(nodes[best - 1].width + nodes[best].width);
        memmove(&nodes[best], &nodes[best + 1],
                (skyline->count - best - 1) * sizeof(muiSkylineNode));
        skyline->count--;
    }
    *xOut = x;
    *yOut = bestY;
    return true;
}
