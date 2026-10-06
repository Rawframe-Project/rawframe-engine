// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Signed distance fields of outlines made of line segments (record
// mui-0006). Whether each pixel's center is inside comes from the winding
// of the segments its row crosses left of it, so overlapping contours are
// their union. Its distance is to the nearest part of a segment on that
// union's edge: segments are cut into pieces of at most a pixel, and the
// places where pieces cross or touch others are found among those nearby.
// Between such places a contour is on the edge or within the union
// throughout, which the windings just either side of it tell; a piece
// others cross is cut there and each part told apart. Only correctly
// rounded float operations are used, so fields are the same bytes on
// every platform.

#ifndef MAUL_UI_SRC_DISTANCE_FIELD_H
#define MAUL_UI_SRC_DISTANCE_FIELD_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// A line segment in pixels, y up.
typedef struct muiSegment
{
    float x0;
    float y0;
    float x1;
    float y1;
} muiSegment;

// Where a row's center line crosses a piece: the place along the row and
// the winding it adds.
typedef struct muiCrossing
{
    float at;
    int32_t winding;
} muiCrossing;

// The field's pixels: left and top are the pixel edges of its first
// column and row, y up; spread is how many pixels a distance reaches.
typedef struct muiFieldGrid
{
    int32_t left;
    int32_t top;
    uint32_t width;
    uint32_t height;
    uint32_t spread;
    // Inside is an odd winding rather than a nonzero one.
    bool evenOdd;
} muiFieldGrid;

// The work memory a field of count pieces needs.
typedef struct muiFieldScratch
{
    // height + 1 entries, and muiCountCrossings.
    uint32_t* rowStarts;
    muiCrossing* crossings;
    // width * height + 1 entries, and count.
    uint32_t* cellStarts;
    uint32_t* cellPieces;
    // muiEdgeRoom entries.
    muiSegment* edge;
    // width * height entries.
    float* distances;
} muiFieldScratch;

// How many pieces of at most a pixel the segments are cut into.
size_t muiCountPieces(const muiSegment* segments, uint32_t count);

// Cuts the segments into muiCountPieces pieces, each noting the segment
// it is part of, and returns how many there are.
uint32_t muiCutPieces(const muiSegment* segments, uint32_t count, muiSegment* pieces,
                      uint32_t* origins);

// How many of the edge's segments count pieces may give.
size_t muiEdgeRoom(uint32_t count);

// How many times the grid's row center lines cross the pieces.
size_t muiCountCrossings(const muiSegment* pieces, uint32_t count, const muiFieldGrid* grid);

// Writes the field of the pieces, a byte per pixel, rows from the top:
// 128 at the outline and 128 / spread more for each pixel inside, less
// outside, rounded and held within 0 and 255.
void muiDrawDistanceField(const muiSegment* pieces, const uint32_t* origins, uint32_t count,
                          const muiFieldGrid* grid, const muiFieldScratch* scratch,
                          unsigned char* pixels);

#endif // MAUL_UI_SRC_DISTANCE_FIELD_H
