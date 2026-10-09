// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Multi-channel distance fields of outlines made of line segments
// (record mui-0006): four channels, red, green and blue whose median is
// the distance with corners kept sharp, and alpha the one-channel field
// (distance_field.h). The union's edge, as the one-channel field finds
// it, is cut into edges at corners and where its chains end; edges are
// coloured as msdfgen colours them, and each colour channel holds the
// signed pseudo-distance to the nearest edge of its colour. Only correctly
// rounded float operations are used, so fields are the same bytes on
// every platform.

#ifndef MAUL_UI_SRC_MULTI_FIELD_H
#define MAUL_UI_SRC_MULTI_FIELD_H

#include "distance_field.h"

#include <stddef.h>
#include <stdint.h>

// The work memory a multi-channel field needs beside muiFieldScratch,
// whose edgeOrigins and edgeSides it needs too.
typedef struct muiMultiScratch
{
    // For each segment the pieces were cut from, the line or curve of the
    // outline it is part of (muiFlattenOutline).
    const uint32_t* curves;
    // muiEdgeRoom entries each: the edge segments' colours, where chains
    // start (and one more entry), chains sorted by their starts, and a
    // loop's edge segments.
    uint8_t* edgeColors;
    uint32_t* chains;
    uint32_t* sorted;
    uint32_t* loop;
    // 9 floats a pixel, aligned for floats.
    void* channels;
    // An entry a pixel.
    unsigned char* inside;
} muiMultiScratch;

// Writes the multi-channel field of the pieces, four bytes per pixel,
// rows from the top: each channel 128 at its distance's 0 and 128 / spread
// more for each pixel inside, less outside, rounded and held within 0 and
// 255; alpha the bytes muiDrawDistanceField writes.
void muiDrawMultiField(const muiSegment* pieces, const uint32_t* origins, uint32_t count,
                       const muiFieldGrid* grid, const muiFieldScratch* scratch,
                       const muiMultiScratch* multi, unsigned char* pixels);

#endif // MAUL_UI_SRC_MULTI_FIELD_H
