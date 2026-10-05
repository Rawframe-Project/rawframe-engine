// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// A skyline bottom-left rectangle packer for one plot of a glyph atlas
// (record mui-0006): the lower edge of what is placed, as segments from
// left to right; each rectangle goes where it sits highest up, then
// furthest left. Coordinates grow down from the plot's top.

#ifndef MAUL_UI_SRC_SKYLINE_H
#define MAUL_UI_SRC_SKYLINE_H

#include <stdbool.h>
#include <stdint.h>

// A segment: from x, width wide, filled down to y.
typedef struct muiSkylineNode
{
    uint16_t x;
    uint16_t y;
    uint16_t width;
} muiSkylineNode;

// The packer of a width by height area, its segments in nodes, which
// hold width of them, as many as there can be.
typedef struct muiSkyline
{
    muiSkylineNode* nodes;
    uint32_t count;
    uint16_t width;
    uint16_t height;
} muiSkyline;

// Empties the area: one segment across it at the top.
void muiSkylineReset(muiSkyline* skyline);

// Places a width by height rectangle and gives its top left; false,
// placing nothing, when it does not fit.
bool muiSkylineInsert(muiSkyline* skyline, uint32_t width, uint32_t height, uint32_t* xOut,
                      uint32_t* yOut);

#endif // MAUL_UI_SRC_SKYLINE_H
