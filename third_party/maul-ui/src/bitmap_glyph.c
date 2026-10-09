// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Colour bitmap glyphs (record mui-0006), in CBLC and CBDT as OpenType has
// them. A strike's index subtables name glyph ranges; each of the five
// index formats finds a glyph's image in CBDT, one of image formats 17
// (small metrics), 18 (big metrics) or 19 (the index's metrics), each a
// PNG. Every offset and length is checked against its table.

#include "bitmap_glyph.h"

#include FT_OUTLINE_H

#include <string.h>

enum
{
    BITMAP_SIZE = 48,
    // A BitmapSize record's fields.
    SIZE_ARRAY = 0,
    SIZE_COUNT = 8,
    SIZE_START = 40,
    SIZE_END = 42,
    SIZE_PPEM_Y = 45,
    SIZE_DEPTH = 46,
    BIG_METRICS = 8,
    SMALL_METRICS = 5
};

// A span of a table, its reads checked.
typedef struct Span
{
    const uint8_t* data;
    size_t size;
} Span;

static bool Has(Span s, size_t at, size_t length)
{
    return at <= s.size && length <= s.size - at;
}

static uint32_t U8(Span s, size_t at)
{
    return Has(s, at, 1) ? s.data[at] : 0;
}

static uint32_t U16(Span s, size_t at)
{
    return Has(s, at, 2) ? (uint32_t)s.data[at] << 8 | s.data[at + 1] : 0;
}

static uint32_t U32(Span s, size_t at)
{
    return Has(s, at, 4) ? (uint32_t)s.data[at] << 24 | (uint32_t)s.data[at + 1] << 16 |
                               (uint32_t)s.data[at + 2] << 8 | s.data[at + 3]
                         : 0;
}

static int32_t I8(Span s, size_t at)
{
    return (int32_t)(int8_t)(uint8_t)U8(s, at);
}

bool muiHasColorBitmaps(const muiFont* font)
{
    return (font->cblc.size > 0 && font->cbdt.size > 0) || font->sbix.size > 0;
}

// Where a glyph's image lies in CBDT: its offset, length and image
// format, and the index's big metrics where it has them.
typedef struct Image
{
    size_t offset;
    size_t length;
    uint32_t format;
    bool hasMetrics;
    size_t metrics;
    Span index;
} Image;

// The index of a glyph in a sorted array of 16-bit glyphs, stride bytes
// apart; count when absent.
static uint32_t Search(Span s, size_t at, uint32_t count, size_t stride, uint32_t glyph)
{
    uint32_t low = 0;
    uint32_t high = count;
    while (low < high)
    {
        uint32_t middle = low + (high - low) / 2;
        uint32_t found = U16(s, at + middle * stride);
        if (found == glyph)
        {
            return middle;
        }
        if (found < glyph)
        {
            low = middle + 1;
        }
        else
        {
            high = middle;
        }
    }
    return count;
}

// Offsets of each glyph of the range and one past: 32 bits (format 1) or
// 16 (format 3).
static bool ByOffsets(Span t, size_t at, uint32_t i, uint32_t width, Image* image)
{
    size_t first = at + (size_t)i * width;
    if (!Has(t, first, (size_t)width * 2))
    {
        return false;
    }
    uint32_t start = width == 4 ? U32(t, first) : U16(t, first);
    uint32_t end = width == 4 ? U32(t, first + 4) : U16(t, first + 2);
    image->offset += start;
    image->length = end > start ? end - start : 0;
    return image->length > 0;
}

// Images of one size: one after another (format 2), or for a sorted list
// of glyphs (format 5); both with big metrics shared.
static bool BySize(Span t, size_t at, uint32_t i, uint32_t glyph, bool listed, Image* image)
{
    uint32_t size = U32(t, at);
    image->hasMetrics = true;
    image->metrics = at + 4;
    if (listed)
    {
        uint32_t count = U32(t, at + 4 + BIG_METRICS);
        size_t list = at + 8 + BIG_METRICS;
        if (!Has(t, list, (size_t)count * 2))
        {
            return false;
        }
        i = Search(t, list, count, 2, glyph);
        if (i == count)
        {
            return false;
        }
    }
    image->offset += (size_t)i * size;
    image->length = size;
    return size > 0;
}

// Glyph and offset pairs, sorted, and one past (format 4).
static bool ByPairs(Span t, size_t at, uint32_t glyph, Image* image)
{
    uint32_t count = U32(t, at);
    size_t pairs = at + 4;
    if (!Has(t, pairs, ((size_t)count + 1) * 4))
    {
        return false;
    }
    uint32_t i = Search(t, pairs, count, 4, glyph);
    if (i == count)
    {
        return false;
    }
    uint32_t start = U16(t, pairs + (size_t)i * 4 + 2);
    uint32_t end = U16(t, pairs + (size_t)i * 4 + 6);
    image->offset += start;
    image->length = end > start ? end - start : 0;
    return image->length > 0;
}

// Finds a glyph in one index subtable, whose range holds it.
static bool InSubtable(Span t, size_t at, uint32_t first, uint32_t glyph, Image* image)
{
    uint32_t indexFormat = U16(t, at);
    *image = (Image){U32(t, at + 4), 0, U16(t, at + 2), false, 0, t};
    if (!Has(t, at, 8))
    {
        return false;
    }
    uint32_t i = glyph - first;
    switch (indexFormat)
    {
    case 1:
        return ByOffsets(t, at + 8, i, 4, image);
    case 2:
        return BySize(t, at + 8, i, glyph, false, image);
    case 3:
        return ByOffsets(t, at + 8, i, 2, image);
    case 4:
        return ByPairs(t, at + 8, glyph, image);
    case 5:
        return BySize(t, at + 8, i, glyph, true, image);
    default:
        return false;
    }
}

// Finds a glyph's image in one strike.
static bool InStrike(Span t, size_t record, uint32_t glyph, Image* image)
{
    if (glyph < U16(t, record + SIZE_START) || glyph > U16(t, record + SIZE_END) ||
        U8(t, record + SIZE_DEPTH) != 32)
    {
        return false;
    }
    size_t array = U32(t, record + SIZE_ARRAY);
    uint32_t count = U32(t, record + SIZE_COUNT);
    if (!Has(t, array, (size_t)count * 8))
    {
        return false;
    }
    for (uint32_t i = 0; i < count; i++)
    {
        size_t entry = array + (size_t)i * 8;
        uint32_t first = U16(t, entry);
        if (glyph >= first && glyph <= U16(t, entry + 2))
        {
            return InSubtable(t, array + U32(t, entry + 4), first, glyph, image);
        }
    }
    return false;
}

// Reads an image's metrics and PNG from CBDT.
static bool ReadImage(Span cbdt, const Image* image, uint32_t ppem, muiBitmapGlyph* out)
{
    if (!Has(cbdt, image->offset, image->length))
    {
        return false;
    }
    Span data = {cbdt.data + image->offset, image->length};
    size_t png = 0;
    switch (image->format)
    {
    case 17:
        out->left = (float)I8(data, 2);
        out->top = (float)I8(data, 3);
        png = SMALL_METRICS;
        break;
    case 18:
        out->left = (float)I8(data, 2);
        out->top = (float)I8(data, 3);
        png = BIG_METRICS;
        break;
    case 19:
        if (!image->hasMetrics)
        {
            return false;
        }
        out->left = (float)I8(image->index, image->metrics + 2);
        out->top = (float)I8(image->index, image->metrics + 3);
        break;
    default:
        return false;
    }
    uint32_t length = U32(data, png);
    if (!Has(data, png + 4, length) || length == 0)
    {
        return false;
    }
    out->png = data.data + png + 4;
    out->size = length;
    out->ppem = ppem;
    return true;
}

// How well a strike's ppem suits a size: smaller is better; the smallest
// reaching it first, then the largest below it.
static uint32_t Rank(uint32_t ppem, float pixelSize)
{
    return (float)ppem >= pixelSize ? ppem : 0x10000u + (0xFFFFu - ppem);
}

// Finds a glyph's bitmap in CBLC and CBDT.
static bool FindInCbdt(const muiFont* font, uint32_t glyph, float pixelSize,
                       muiBitmapGlyph* glyphOut)
{
    if (font->cblc.size == 0 || font->cbdt.size == 0)
    {
        return false;
    }
    Span cblc = {font->cblc.data, font->cblc.size};
    Span cbdt = {font->cbdt.data, font->cbdt.size};
    uint32_t sizes = U32(cblc, 4);
    if (U16(cblc, 0) < 2 || !Has(cblc, 8, (size_t)sizes * BITMAP_SIZE))
    {
        return false;
    }
    bool found = false;
    uint32_t best = UINT32_MAX;
    for (uint32_t i = 0; i < sizes; i++)
    {
        size_t record = 8 + (size_t)i * BITMAP_SIZE;
        uint32_t ppem = U8(cblc, record + SIZE_PPEM_Y);
        uint32_t rank = Rank(ppem, pixelSize);
        Image image;
        muiBitmapGlyph candidate;
        if (ppem > 0 && rank < best && InStrike(cblc, record, glyph, &image) &&
            ReadImage(cbdt, &image, ppem, &candidate))
        {
            *glyphOut = candidate;
            best = rank;
            found = true;
        }
    }
    return found;
}

// A PNG's height, from its header; 0 for what is not a PNG.
static uint32_t PngHeight(Span png)
{
    static const uint8_t SIGNATURE[8] = {137, 80, 78, 71, 13, 10, 26, 10};
    bool isPng =
        Has(png, 0, 24) && memcmp(png.data, SIGNATURE, 8) == 0 && U32(png, 12) == 0x49484452u;
    return isPng ? U32(png, 20) : 0;
}

// sbix: a header of a version, flags, a strike count and their offsets;
// a strike of a ppem, a ppi and an offset for every glyph and one past;
// a glyph's data of an origin offset x and y, a graphic type and the
// graphic. 'dupe' names another glyph whose data is used, followed at
// most SBIX_DUPES times, as FreeType does.
enum
{
    SBIX_DUPES = 4,
    TAG_PNG = 0x706E6720u,
    TAG_DUPE = 0x64757065u
};

// A glyph's graphic in one strike: its PNG and its origin offsets.
static bool InSbixStrike(Span t, size_t strike, uint32_t glyphCount, uint32_t glyph, Span* pngOut,
                         int32_t* xOut, int32_t* yOut)
{
    for (uint32_t hop = 0; hop <= SBIX_DUPES && glyph < glyphCount; hop++)
    {
        size_t offsets = strike + 4 + (size_t)glyph * 4;
        uint32_t start = U32(t, offsets);
        uint32_t end = U32(t, offsets + 4);
        if (!Has(t, offsets, 8) || end < start + 8 || !Has(t, strike + start, end - start))
        {
            return false;
        }
        Span data = {t.data + strike + start, end - start};
        uint32_t type = U32(data, 4);
        if (type == TAG_DUPE)
        {
            glyph = U16(data, 8);
            continue;
        }
        *pngOut = (Span){data.data + 8, data.size - 8};
        *xOut = (int32_t)(int16_t)(uint16_t)U16(data, 0);
        *yOut = (int32_t)(int16_t)(uint16_t)U16(data, 2);
        return type == TAG_PNG && PngHeight(*pngOut) > 0;
    }
    return false;
}

// Where an sbix graphic's origin lies from the pen, in font units: the
// glyph's outline box's lower left corner where it has contours, else
// the pen itself.
static void SbixOrigin(const muiFont* font, uint32_t glyph, double* xOut, double* yOut)
{
    *xOut = 0.0;
    *yOut = 0.0;
    FT_Face face = font->face;
    if (FT_IS_SCALABLE(face) &&
        FT_Load_Glyph(face, glyph, FT_LOAD_NO_SCALE | FT_LOAD_NO_BITMAP | FT_LOAD_NO_HINTING) ==
            0 &&
        face->glyph->format == FT_GLYPH_FORMAT_OUTLINE && face->glyph->outline.n_contours > 0)
    {
        FT_BBox box;
        FT_Outline_Get_CBox(&face->glyph->outline, &box);
        *xOut = (double)box.xMin;
        *yOut = (double)box.yMin;
    }
}

static bool FindInSbix(const muiFont* font, uint32_t glyph, float pixelSize,
                       muiBitmapGlyph* glyphOut)
{
    Span t = {font->sbix.data, font->sbix.size};
    uint32_t strikes = U32(t, 4);
    if (U16(t, 0) < 1 || !Has(t, 8, (size_t)strikes * 4))
    {
        return false;
    }
    bool found = false;
    uint32_t best = UINT32_MAX;
    for (uint32_t i = 0; i < strikes; i++)
    {
        size_t strike = U32(t, 8 + (size_t)i * 4);
        uint32_t ppem = U16(t, strike);
        uint32_t rank = Rank(ppem, pixelSize);
        Span png;
        int32_t x = 0;
        int32_t y = 0;
        if (ppem > 0 && rank < best &&
            InSbixStrike(t, strike, font->metrics.glyphCount, glyph, &png, &x, &y))
        {
            *glyphOut = (muiBitmapGlyph){png.data, (uint32_t)png.size, ppem, (float)x,
                                         (float)(y + (int32_t)PngHeight(png))};
            best = rank;
            found = true;
        }
    }
    if (found)
    {
        double originX = 0.0;
        double originY = 0.0;
        SbixOrigin(font, glyph, &originX, &originY);
        double scale = (double)glyphOut->ppem / (double)font->metrics.unitsPerEm;
        glyphOut->left += (float)(originX * scale);
        glyphOut->top += (float)(originY * scale);
    }
    return found;
}

bool muiFindBitmapGlyph(const muiFont* font, uint32_t glyph, float pixelSize,
                        muiBitmapGlyph* glyphOut)
{
    return FindInCbdt(font, glyph, pixelSize, glyphOut) ||
           (font->sbix.size > 0 && FindInSbix(font, glyph, pixelSize, glyphOut));
}
