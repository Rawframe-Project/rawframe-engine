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
#include "maul-ui/text_style.h"

#include <ft2build.h>
#include FT_FREETYPE_H
#include <hb.h>
#include <stdbool.h>
#include <stdint.h>

enum
{
    // The axes a font's instances set; those past them keep their
    // defaults.
    MUI_MAX_FONT_AXES = 16,
    // The instances other than the font's own kept made for shaping.
    MUI_SHAPING_SLOTS = 4
};

// A variation axis: its tag and range, in 16.16.
typedef struct muiFontAxis
{
    uint32_t tag;
    FT_Fixed minimum;
    FT_Fixed defaultValue;
    FT_Fixed maximum;
} muiFontAxis;

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
    // The instance the face was last set to for glyph images, as a key's
    // top bits.
    uint64_t imageInstance;
    muiFontAxis axes[MUI_MAX_FONT_AXES];
    uint32_t axisCount;
    // OS/2 usWeightClass, 400 without one, usWidthClass, 5 (normal)
    // without one, and the face's slant: mui_slantNormal, Italic or
    // Oblique.
    uint32_t weightClass;
    uint32_t widthClass;
    muiFontSlant faceSlant;
    // Shaping fonts of other instances, by their key's top bits, replaced
    // in turn.
    uint64_t shaperInstances[MUI_SHAPING_SLOTS];
    hb_font_t* shapers[MUI_SHAPING_SLOTS];
    uint32_t nextShaper;
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
