// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The fonts of a text service (record mui-0006): each font's bytes, the
// faces FreeType and HarfBuzz read from them, and the metrics read when
// it was made.

#ifndef MAUL_UI_SRC_FONT_STORE_H
#define MAUL_UI_SRC_FONT_STORE_H

#include "pool.h"

#include "maul-ui/font.h"

#include <ft2build.h>
#include FT_FREETYPE_H
#include <hb.h>

typedef struct muiFont
{
    // The file's bytes: copy when the service made one, else the
    // caller's.
    const unsigned char* data;
    unsigned char* copy;
    size_t size;
    FT_Face face;
    hb_face_t* shapingFace;
    // At a scale of the units per em, so positions are the font's own.
    hb_font_t* shapingFont;
    muiFontMetrics metrics;
    // The size the face was last set to for glyph images, in 64ths of a
    // pixel; 0 before any.
    long imageSize;
} muiFont;

typedef struct muiFontStore
{
    muiPool pool;
    // Font i is fonts[i - 1].
    muiFont* fonts;
} muiFontStore;

// Destroys what a font record holds, which may be partly made, and
// zeroes it.
void muiReleaseFont(const muiAllocator* allocator, muiFont* font);

#endif // MAUL_UI_SRC_FONT_STORE_H
