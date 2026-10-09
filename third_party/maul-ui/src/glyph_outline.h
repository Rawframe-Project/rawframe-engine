// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// A glyph's outline as FreeType loads it for images (record mui-0006):
// the font of a key, its instance set, the outline at a size unhinted and
// without embedded bitmaps, synthetic oblique and bold made, and coverage
// rasterized into a box of whole pixels. Glyph images and colour glyphs
// share it.

#ifndef MAUL_UI_SRC_GLYPH_OUTLINE_H
#define MAUL_UI_SRC_GLYPH_OUTLINE_H

#include "text_service.h"

#include <stdbool.h>
#include <stdint.h>

enum
{
    // The widest and tallest image rendered, in pixels: FreeType's
    // rasterizer works in 16-bit pixel coordinates.
    MUI_MAX_IMAGE_EXTENT = 32767
};

// A box of whole pixels, y up: its left and bottom, width and height.
typedef struct muiPixelBox
{
    FT_Pos left;
    FT_Pos bottom;
    FT_Pos width;
    FT_Pos height;
} muiPixelBox;

// FreeType's error as a result: out of memory is capacity, anything else
// format.
muiResult muiGlyphFailed(FT_Error error);

// Whether an em in pixels is one images are rendered at.
bool muiIsGlyphSizeValid(float pixelSize);

// The font of a key, checked to have the glyph: NULL with the result
// otherwise; keyOut receives the key with its default resolved.
muiFont* muiGlyphFontOf(const muiTextService* service, uint64_t font, uint32_t glyph,
                        uint64_t* keyOut, muiResult* result);

// Loads a glyph's outline into the font's face, in a key's instance at a
// size in 64ths of a pixel, the pen moved right by offset 64ths;
// mui_empty for a face without outlines, a bitmap-only font's.
muiResult muiLoadGlyphOutline(muiFont* font, uint64_t key, uint32_t glyph, long size,
                              FT_Pos offset);

// The whole pixels a box in 64ths of a pixel touches.
muiPixelBox muiPixelBoxOf(FT_BBox box);

// The whole pixels an outline touches; all 0 for an empty one.
muiPixelBox muiOutlineBox(const FT_Outline* outline);

// Joins a box into another, a box without area adding nothing.
void muiJoinPixelBox(muiPixelBox* box, muiPixelBox own);

// Renders an outline's coverage into a box's bytes, rows from the top,
// pitch bytes a row: the outline is moved to the box. The bytes the
// outline does not cover are left as they are.
muiResult muiRasterizeOutline(const muiTextService* service, FT_Outline* outline,
                              const muiPixelBox* box, unsigned char* pixels, int pitch);

#endif // MAUL_UI_SRC_GLYPH_OUTLINE_H
