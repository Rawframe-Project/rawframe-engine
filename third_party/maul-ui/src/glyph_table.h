// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The glyph atlas's lookups (record mui-0006): entries keyed by font,
// glyph, size and subpixel position, in an open-addressing table with
// linear probing, at most half full. An entry names the plot it is in
// and that plot's generation; evicting a plot raises its generation, so
// its entries go stale at once and are dropped when the table is
// rebuilt.

#ifndef MAUL_UI_SRC_GLYPH_TABLE_H
#define MAUL_UI_SRC_GLYPH_TABLE_H

#include "maul-ui/base.h"

#include <stdbool.h>
#include <stdint.h>

enum
{
    // An entry's plot when its glyph has no image to place.
    MUI_NO_PLOT = UINT32_MAX
};

typedef struct muiGlyphKey
{
    uint64_t font;
    uint32_t glyph;
    // The size in 64ths of a pixel, times 4, plus the quarter-pixel
    // position.
    uint32_t sizeBin;
} muiGlyphKey;

typedef struct muiAtlasEntry
{
    muiGlyphKey key;
    // The plot, from 1, MUI_NO_PLOT, or 0 for an empty slot.
    uint32_t plot;
    uint32_t generation;
    // The image's top left in its page and its size, and where it goes
    // from the pen and the baseline.
    uint16_t u;
    uint16_t v;
    uint16_t width;
    uint16_t height;
    int32_t left;
    int32_t top;
} muiAtlasEntry;

typedef struct muiGlyphTable
{
    muiAtlasEntry* entries;
    // A power of two, or 0 before the first entry.
    uint32_t capacity;
    uint32_t count;
} muiGlyphTable;

// Whether an entry's plot was evicted since it was placed: generations
// holds each plot's, plot 1 at index 0.
bool muiIsEntryStale(const muiAtlasEntry* entry, const uint32_t* generations);

// The entry with key, live or stale, or the empty slot it would take;
// the table has room for one more (muiReserveEntry).
muiAtlasEntry* muiFindEntry(const muiGlyphTable* table, const muiGlyphKey* key);

// Makes room for one more entry, rebuilding the table without stale
// entries, larger when the live ones need it; false when memory runs
// out, leaving the table as it was.
bool muiReserveEntry(const muiAllocator* allocator, muiGlyphTable* table,
                     const uint32_t* generations);

void muiFreeGlyphTable(const muiAllocator* allocator, muiGlyphTable* table);

#endif // MAUL_UI_SRC_GLYPH_TABLE_H
