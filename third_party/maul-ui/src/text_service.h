// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The text service (record mui-0006): one block of its own and its font
// records, a FreeType library whose memory is the service's allocator,
// and HarfBuzz's Unicode functions from Maul Unicode.

#ifndef MAUL_UI_SRC_TEXT_SERVICE_H
#define MAUL_UI_SRC_TEXT_SERVICE_H

#include "family_store.h"
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
    muiFamilyStore families;
    // What font key 0 names; the null id for none.
    muiFontId defaultFont;
    // Keys of fonts and families tried, in order, after a style's own for
    // characters they lack.
    uint64_t fallbacks[MUI_MAX_FALLBACKS];
    uint32_t fallbackCount;
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
    // A line's grapheme clusters as hit testing finds them.
    muiBuffer hitBoxes;
    // Scratch for distance fields: the outline's segments, their pieces
    // with the segment of each, the row starts and crossings, the cell
    // starts and pieces, the edge, and each pixel's squared distance.
    muiBuffer fieldSegments;
    muiBuffer fieldPieces;
    muiBuffer fieldOrigins;
    muiBuffer fieldRows;
    muiBuffer fieldCrossings;
    muiBuffer fieldCells;
    muiBuffer fieldCellPieces;
    muiBuffer fieldEdge;
    muiBuffer fieldDistances;
};

// The font a key names, key 0 naming the default font, and the key it
// resolves to; NULL when it names none.
muiFont* muiFindFont(const muiTextService* service, uint64_t key, uint64_t* keyOut);

#endif // MAUL_UI_SRC_TEXT_SERVICE_H
