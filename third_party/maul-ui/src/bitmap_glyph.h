// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Colour bitmap glyphs (record mui-0006): a glyph's PNG found in a font's
// CBLC and CBDT tables, or its sbix table, read in place, from the strike
// that suits a size.

#ifndef MAUL_UI_SRC_BITMAP_GLYPH_H
#define MAUL_UI_SRC_BITMAP_GLYPH_H

#include "font_store.h"

#include <stdbool.h>
#include <stdint.h>

// A glyph's colour bitmap: its PNG, its strike's ppem, and where the
// PNG's top left lies from the pen, in the strike's pixels, y up.
typedef struct muiBitmapGlyph
{
    const uint8_t* png;
    uint32_t size;
    uint32_t ppem;
    float left;
    float top;
} muiBitmapGlyph;

// Whether the font has colour bitmaps at all.
bool muiHasColorBitmaps(const muiFont* font);

// Finds a glyph's colour bitmap for an em of pixelSize, from CBDT, else
// from sbix: from the smallest strike that reaches it, else the largest,
// passing over strikes without the glyph's PNG; false for none, or for
// tables that do not hold together.
bool muiFindBitmapGlyph(const muiFont* font, uint32_t glyph, float pixelSize,
                        muiBitmapGlyph* glyphOut);

#endif // MAUL_UI_SRC_BITMAP_GLYPH_H
