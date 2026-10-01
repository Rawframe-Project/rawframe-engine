// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The text service (record mui-0006): one block of its own and its font
// records, a FreeType library whose memory is the service's allocator,
// and HarfBuzz's Unicode functions from Maul Unicode.

#ifndef MAUL_UI_SRC_TEXT_SERVICE_H
#define MAUL_UI_SRC_TEXT_SERVICE_H

#include "font_store.h"
#include "text_block.h"

#include "maul-ui/text.h"

#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_MODULE_H
#include FT_SYSTEM_H
#include <hb.h>

struct muiTextService
{
    muiAllocator allocator;
    size_t blockSize;
    muiTextLimits limits;
    // FreeType keeps a pointer to it, so it lives in the service.
    struct FT_MemoryRec_ memory;
    FT_Library freetype;
    hb_unicode_funcs_t* unicode;
    muiFontStore fonts;
    muiTextBlockStore blocks;
    // What font key 0 names; the null id for none.
    muiFontId defaultFont;
    // Blocks that could not be laid out for want of memory.
    uint64_t failures;
    // Scratch for laying out and painting: lines, bidi runs, glyphs and
    // bidi resolution's workspace.
    muiBuffer lines;
    muiBuffer runs;
    muiBuffer glyphs;
    muiBuffer workspace;
    // A line shaped on its own: its items and glyphs.
    muiBuffer lineItems;
    muiBuffer lineGlyphs;
};

#endif // MAUL_UI_SRC_TEXT_SERVICE_H
