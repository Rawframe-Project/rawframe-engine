// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// What colour glyphs of either COLR version are painted with (record
// mui-0006): the font and size, the palette, the text's colour, and the
// colours of palette entries in premultiplied linear light.

#ifndef MAUL_UI_SRC_PAINT_SOURCE_H
#define MAUL_UI_SRC_PAINT_SOURCE_H

#include "glyph_outline.h"

#include FT_COLOR_H

#include <stdint.h>

// Whether FreeType reads version 1 graphs: from 2.13, which an installed
// FreeType may be older than.
#define MUI_COLR_PAINT (FREETYPE_MAJOR > 2 || (FREETYPE_MAJOR == 2 && FREETYPE_MINOR >= 13))

enum
{
    // The palette entry that stands for the text's colour.
    MUI_FOREGROUND_ENTRY = 0xFFFF
};

// What a colour glyph is painted with, of either version.
typedef struct muiPaintSource
{
    muiTextService* service;
    muiFont* font;
    // The font key with its default resolved, as muiGlyphFontOf gives it.
    uint64_t key;
    // The em in 64ths of a pixel, and how far the pen is right of a pixel
    // boundary in 64ths.
    long size;
    FT_Pos offset;
    // The palette's colours, entries of them, NULL for none.
    const FT_Color* palette;
    uint32_t entries;
    // The text's colour, linear and premultiplied.
    muiLinearColor foreground;
} muiPaintSource;

// A palette entry's colour, premultiplied linear: sRGB with straight
// alpha in the palette, the text's colour for MUI_FOREGROUND_ENTRY, and
// transparent for an entry past the palette or with no palette.
muiLinearColor muiPaletteColor(const FT_Color* palette, uint32_t entries, uint32_t index,
                               muiLinearColor foreground);

#if MUI_COLR_PAINT

// A version 1 paint's colour, premultiplied linear: its palette entry's
// times its alpha, a 2.14 number.
muiLinearColor muiColrPaintColor(const muiPaintSource* source, FT_ColorIndex index);

#endif

#endif // MAUL_UI_SRC_PAINT_SOURCE_H
