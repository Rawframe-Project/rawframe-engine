// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// COLR version 1 paint graphs (record mui-0006): a glyph's graph walked
// from its root, each paint evaluated into a premultiplied linear-light
// surface over the glyph's box of pixels, transforms composed on the way
// down from font units, a PaintGlyph's outline a FreeType coverage mask
// over its child, layers composited source over. At most
// MUI_MAX_PAINT_DEPTH paints deep, so a cycle in a damaged font ends.

#ifndef MAUL_UI_SRC_COLR_PAINT_H
#define MAUL_UI_SRC_COLR_PAINT_H

#include "paint_source.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#if MUI_COLR_PAINT

enum
{
    MUI_MAX_PAINT_DEPTH = 64
};

// Whether a glyph has a version 1 graph, and its root.
bool muiFindColorPaint(FT_Face face, uint32_t glyph, FT_OpaquePaint* rootOut);

// The box of whole pixels a glyph's graph paints: its clip box where the
// font has one, else the joint box of its outlines under their transforms;
// a width of 0 for nothing.
muiResult muiColorPaintBox(const muiPaintSource* source, uint32_t glyph, FT_OpaquePaint root,
                           muiPixelBox* boxOut);

// Paints a glyph's graph over a box into premultiplied linear pixels, four
// floats a pixel, rows from the top, nothing outside its clip box.
muiResult muiPaintColorGlyph(const muiPaintSource* source, uint32_t glyph, FT_OpaquePaint root,
                             const muiPixelBox* box, float* pixels);

#endif

#endif // MAUL_UI_SRC_COLR_PAINT_H
