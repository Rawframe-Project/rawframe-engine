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
    // Calls refused as invalid input against the service or its atlases.
    uint64_t misuse;
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
    // What an editing block's rules let in of text going in.
    muiBuffer editScratch;
    // Scratch for distance fields: the outline's segments, their pieces
    // with the segment of each, the row starts and crossings, the cell
    // starts and pieces, the edge, and each pixel's squared distance.
    muiBuffer fieldSegments;
    muiBuffer fieldPieces;
    muiBuffer fieldOrigins;
    muiBuffer fieldRows;
    muiBuffer fieldCrossings;
    muiBuffer fieldCells;
    // For multi-channel fields: each segment's line or curve, each edge
    // segment's origin, side and colour, each pixel's channels and
    // whether its center is inside.
    muiBuffer fieldCurves;
    muiBuffer fieldEdgeOrigins;
    muiBuffer fieldEdgeSides;
    muiBuffer fieldEdgeColors;
    // Chains, chains sorted and a loop: three muiEdgeRoom spans and one.
    muiBuffer fieldLoops;
    muiBuffer fieldChannels;
    muiBuffer fieldInside;
    // For colour glyphs: a layer's coverage, the composited pixels, and a
    // version 1 graph's surfaces below the first.
    muiBuffer colorCoverage;
    muiBuffer colorPixels;
    muiBuffer paintSurfaces;
    // A version 1 gradient's colour stops, sorted.
    muiBuffer paintStops;
    // For colour bitmaps: the PNG's data on the way, its RGBA, and its
    // pixels premultiplied in linear light.
    muiBuffer bitmapScratch;
    muiBuffer bitmapRgba;
    muiBuffer bitmapLinear;
    muiBuffer fieldCellPieces;
    muiBuffer fieldEdge;
    muiBuffer fieldDistances;
};

// The font a key names, key 0 naming the default font, and the key it
// resolves to; NULL when it names none.
muiFont* muiFindFont(const muiTextService* service, uint64_t key, uint64_t* keyOut);

// Refuses invalid input, counting it as the service's misuse; a NULL
// service, having nowhere to count, is refused uncounted.
static inline muiResult muiRefuseText(muiTextService* service)
{
    if (service != nullptr)
    {
        service->misuse++;
    }
    return mui_errorInvalid;
}

// A status passed on from a check that could not count: invalid input
// counted as the service's misuse.
static inline muiResult muiCountText(muiTextService* service, muiResult status)
{
    return status == mui_errorInvalid ? muiRefuseText(service) : status;
}

#endif // MAUL_UI_SRC_TEXT_SERVICE_H
