// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Glyph atlases (record mui-0006): pages made as needed, cut into plots
// each packed by a skyline; whole plots are evicted, least recently used
// first, never one the current frame uses. Each image keeps a pixel of
// empty gutter on every side, so filtering never reads a neighbor.

#include "maul-ui/glyph_atlas.h"

#include "allocator.h"
#include "bitmap_glyph.h"
#include "color.h"
#include "font_store.h"
#include "glyph_table.h"
#include "skyline.h"
#include "text_service.h"

#include "maul-ui/glyph_image.h"

#include <math.h>
#include <stdalign.h>
#include <string.h>

#define GLYPH_ATLAS_DEF_COOKIE 0x6D756761u // "muga"

enum
{
    MIN_PAGE = 64,
    MAX_PAGE = 16384,
    MIN_PLOT = 16,
    MAX_PLOT = 4096,
    MAX_PLOTS_PER_PAGE = 4096,
    MAX_PAGES = 64,
    // A pixel on each side of an image.
    GUTTER = 2,
    // A key's form: a field's spread above its size and position.
    SPREAD_SHIFT = 24,
    SIZE_MASK = (1 << SPREAD_SHIFT) - 1
};

// The largest pen coordinate, so pixel positions stay exact.
static const float MAX_PEN = 16777216.0f;

typedef struct Plot
{
    muiSkyline skyline;
    // The frame that last used a glyph of the plot.
    uint64_t lastUse;
    // Its page, kept so a lookup does not divide.
    uint32_t page;
    // The rectangle of the page that changed, when listed.
    uint16_t dirtyX0;
    uint16_t dirtyY0;
    uint16_t dirtyX1;
    uint16_t dirtyY1;
    bool listed;
} Plot;

struct muiGlyphAtlas
{
    muiTextService* service;
    muiAllocator allocator;
    size_t blockSize;
    uint32_t pageWidth;
    uint32_t pageHeight;
    uint32_t plotWidth;
    uint32_t plotHeight;
    uint32_t maxPages;
    // The pages' format, and bytes a pixel: 1, or 4 for an atlas of four
    // channels or of colour glyphs.
    muiAtlasFormat format;
    uint32_t channels;
    uint32_t plotsAcross;
    uint32_t plotsPerPage;
    uint32_t pageCount;
    // Each page's pixels and its plots' skyline segments, maxPages each.
    unsigned char** pages;
    muiSkylineNode** pageNodes;
    // Every plot of every page; plot p (from 1) is plots[p - 1].
    Plot* plots;
    uint32_t* generations;
    // The plots, from 0, whose changes are not taken yet.
    uint32_t* dirty;
    uint32_t dirtyCount;
    // The plot last packed into, from 1; 0 for none.
    uint32_t lastPlot;
    uint64_t frame;
    // A rendered image before it is copied into its page.
    unsigned char* scratch;
    muiGlyphTable table;
};

muiGlyphAtlasDef muiDefaultGlyphAtlasDef(void)
{
    return (muiGlyphAtlasDef){
        .cookie = GLYPH_ATLAS_DEF_COOKIE,
        .pageWidth = 1024,
        .pageHeight = 1024,
        .plotWidth = 256,
        .plotHeight = 256,
        .maxPages = 4,
        .format = mui_atlasOneChannel,
    };
}

static bool IsDefValid(const muiGlyphAtlasDef* def)
{
    bool pages = def->pageWidth >= MIN_PAGE && def->pageWidth <= MAX_PAGE &&
                 def->pageHeight >= MIN_PAGE && def->pageHeight <= MAX_PAGE;
    bool plots = def->plotWidth >= MIN_PLOT && def->plotWidth <= MAX_PLOT &&
                 def->plotHeight >= MIN_PLOT && def->plotHeight <= MAX_PLOT;
    return pages && plots && def->plotWidth <= def->pageWidth &&
           def->plotHeight <= def->pageHeight && def->pageWidth % def->plotWidth == 0 &&
           def->pageHeight % def->plotHeight == 0 &&
           (def->pageWidth / def->plotWidth) * (def->pageHeight / def->plotHeight) <=
               MAX_PLOTS_PER_PAGE &&
           def->maxPages >= 1 && def->maxPages <= MAX_PAGES &&
           (def->format == mui_atlasOneChannel || def->format == mui_atlasFourChannel ||
            def->format == mui_atlasColor);
}

muiResult muiCreateGlyphAtlas(muiTextService* service, const muiGlyphAtlasDef* def,
                              muiGlyphAtlas** atlasOut)
{
    if (atlasOut != nullptr)
    {
        *atlasOut = nullptr;
    }
    if (service == nullptr || def == nullptr || atlasOut == nullptr ||
        def->cookie != GLYPH_ATLAS_DEF_COOKIE || !IsDefValid(def))
    {
        return muiRefuseText(service);
    }
    uint32_t plotsPerPage = (def->pageWidth / def->plotWidth) * (def->pageHeight / def->plotHeight);
    size_t plots = (size_t)plotsPerPage * def->maxPages;
    muiLayout layout = {0, false};
    (void)muiLayoutAdd(&layout, 1, sizeof(muiGlyphAtlas), alignof(muiGlyphAtlas));
    size_t pagesAt =
        muiLayoutAdd(&layout, def->maxPages, sizeof(unsigned char*), alignof(unsigned char*));
    size_t nodesAt =
        muiLayoutAdd(&layout, def->maxPages, sizeof(muiSkylineNode*), alignof(muiSkylineNode*));
    size_t plotsAt = muiLayoutAdd(&layout, plots, sizeof(Plot), alignof(Plot));
    size_t generationsAt = muiLayoutAdd(&layout, plots, sizeof(uint32_t), alignof(uint32_t));
    size_t dirtyAt = muiLayoutAdd(&layout, plots, sizeof(uint32_t), alignof(uint32_t));
    uint32_t channels = def->format == mui_atlasOneChannel ? 1u : 4u;
    size_t scratchAt =
        muiLayoutAdd(&layout, (size_t)def->plotWidth * def->plotHeight * channels, 1, 1);
    unsigned char* block = muiAllocate(&service->allocator, layout.size, alignof(muiGlyphAtlas));
    if (block == nullptr)
    {
        return mui_errorCapacity;
    }
    memset(block, 0, layout.size);
    muiGlyphAtlas* atlas = (muiGlyphAtlas*)block;
    *atlas = (muiGlyphAtlas){
        .service = service,
        .allocator = service->allocator,
        .blockSize = layout.size,
        .pageWidth = def->pageWidth,
        .pageHeight = def->pageHeight,
        .plotWidth = def->plotWidth,
        .plotHeight = def->plotHeight,
        .maxPages = def->maxPages,
        .format = def->format,
        .channels = channels,
        .plotsAcross = def->pageWidth / def->plotWidth,
        .plotsPerPage = plotsPerPage,
        .pages = (unsigned char**)(block + pagesAt),
        .pageNodes = (muiSkylineNode**)(block + nodesAt),
        .plots = (Plot*)(block + plotsAt),
        .generations = (uint32_t*)(block + generationsAt),
        .dirty = (uint32_t*)(block + dirtyAt),
        .frame = 1,
        .scratch = block + scratchAt,
    };
    *atlasOut = atlas;
    return mui_success;
}

static size_t PageBytes(const muiGlyphAtlas* atlas)
{
    return (size_t)atlas->pageWidth * atlas->pageHeight * atlas->channels;
}

static size_t NodeBytes(const muiGlyphAtlas* atlas)
{
    return (size_t)atlas->plotsPerPage * atlas->plotWidth * sizeof(muiSkylineNode);
}

void muiDestroyGlyphAtlas(muiGlyphAtlas* atlas)
{
    if (atlas == nullptr)
    {
        return;
    }
    muiAllocator allocator = atlas->allocator;
    for (uint32_t i = 0; i < atlas->pageCount; i++)
    {
        muiRelease(&allocator, atlas->pages[i], PageBytes(atlas), 1);
        muiRelease(&allocator, atlas->pageNodes[i], NodeBytes(atlas), alignof(muiSkylineNode));
    }
    muiFreeGlyphTable(&allocator, &atlas->table);
    muiRelease(&allocator, atlas, atlas->blockSize, alignof(muiGlyphAtlas));
}

void muiGlyphAtlas_NextFrame(muiGlyphAtlas* atlas)
{
    if (atlas != nullptr)
    {
        atlas->frame++;
    }
}

// A plot's top left in its page.
static void PlotOrigin(const muiGlyphAtlas* atlas, uint32_t plot, uint32_t* xOut, uint32_t* yOut)
{
    uint32_t within = (plot - 1) % atlas->plotsPerPage;
    *xOut = within % atlas->plotsAcross * atlas->plotWidth;
    *yOut = within / atlas->plotsAcross * atlas->plotHeight;
}

static unsigned char* PageOf(const muiGlyphAtlas* atlas, uint32_t plot)
{
    return atlas->pages[(plot - 1) / atlas->plotsPerPage];
}

static bool AddPage(muiGlyphAtlas* atlas)
{
    size_t pixelBytes = PageBytes(atlas);
    unsigned char* pixels = muiAllocate(&atlas->allocator, pixelBytes, 1);
    muiSkylineNode* nodes =
        muiAllocate(&atlas->allocator, NodeBytes(atlas), alignof(muiSkylineNode));
    if (pixels == nullptr || nodes == nullptr)
    {
        if (pixels != nullptr)
        {
            muiRelease(&atlas->allocator, pixels, pixelBytes, 1);
        }
        if (nodes != nullptr)
        {
            muiRelease(&atlas->allocator, nodes, NodeBytes(atlas), alignof(muiSkylineNode));
        }
        return false;
    }
    memset(pixels, 0, pixelBytes);
    uint32_t page = atlas->pageCount++;
    atlas->pages[page] = pixels;
    atlas->pageNodes[page] = nodes;
    for (uint32_t i = 0; i < atlas->plotsPerPage; i++)
    {
        Plot* plot = &atlas->plots[page * atlas->plotsPerPage + i];
        plot->skyline = (muiSkyline){nodes + (size_t)i * atlas->plotWidth, 0,
                                     (uint16_t)atlas->plotWidth, (uint16_t)atlas->plotHeight};
        plot->page = page;
        muiSkylineReset(&plot->skyline);
    }
    return true;
}

// Empties a plot: its entries go stale and its pixels are cleared, so
// gutters stay empty.
static void Evict(muiGlyphAtlas* atlas, uint32_t plot)
{
    atlas->generations[plot - 1]++;
    muiSkylineReset(&atlas->plots[plot - 1].skyline);
    uint32_t x = 0;
    uint32_t y = 0;
    PlotOrigin(atlas, plot, &x, &y);
    unsigned char* pixels = PageOf(atlas, plot);
    for (uint32_t row = 0; row < atlas->plotHeight; row++)
    {
        memset(pixels + ((size_t)(y + row) * atlas->pageWidth + x) * atlas->channels, 0,
               (size_t)atlas->plotWidth * atlas->channels);
    }
}

static bool TryPlot(muiGlyphAtlas* atlas, uint32_t plot, uint32_t width, uint32_t height,
                    uint32_t* xOut, uint32_t* yOut)
{
    uint32_t x = 0;
    uint32_t y = 0;
    if (!muiSkylineInsert(&atlas->plots[plot - 1].skyline, width, height, &x, &y))
    {
        return false;
    }
    uint32_t originX = 0;
    uint32_t originY = 0;
    PlotOrigin(atlas, plot, &originX, &originY);
    *xOut = originX + x;
    *yOut = originY + y;
    atlas->lastPlot = plot;
    return true;
}

// Finds room for a width by height rectangle: the plot last packed into,
// then every plot made, then a new page, then the least recently used
// plot this frame has not used.
static muiResult Pack(muiGlyphAtlas* atlas, uint32_t width, uint32_t height, uint32_t* plotOut,
                      uint32_t* xOut, uint32_t* yOut)
{
    uint32_t made = atlas->pageCount * atlas->plotsPerPage;
    uint32_t hint = atlas->lastPlot;
    for (uint32_t i = 0; i <= made; i++)
    {
        uint32_t plot = i == 0 ? hint : i;
        if (plot != 0 && (i == 0 || plot != hint) &&
            TryPlot(atlas, plot, width, height, xOut, yOut))
        {
            *plotOut = plot;
            return mui_success;
        }
    }
    uint32_t plot = 0;
    if (atlas->pageCount < atlas->maxPages)
    {
        if (!AddPage(atlas))
        {
            return mui_errorCapacity;
        }
        plot = made + 1;
    }
    else
    {
        uint64_t oldest = atlas->frame;
        for (uint32_t i = 1; i <= made; i++)
        {
            if (atlas->plots[i - 1].lastUse < oldest)
            {
                oldest = atlas->plots[i - 1].lastUse;
                plot = i;
            }
        }
        if (plot == 0)
        {
            return mui_errorCapacity;
        }
        Evict(atlas, plot);
    }
    // An empty plot holds any image that fits a plot.
    (void)TryPlot(atlas, plot, width, height, xOut, yOut);
    *plotOut = plot;
    return mui_success;
}

static void MarkChanged(muiGlyphAtlas* atlas, uint32_t plot, uint32_t x, uint32_t y, uint32_t width,
                        uint32_t height)
{
    Plot* record = &atlas->plots[plot - 1];
    uint16_t x0 = (uint16_t)x;
    uint16_t y0 = (uint16_t)y;
    uint16_t x1 = (uint16_t)(x + width);
    uint16_t y1 = (uint16_t)(y + height);
    if (!record->listed)
    {
        atlas->dirty[atlas->dirtyCount++] = plot - 1;
        record->listed = true;
        record->dirtyX0 = x0;
        record->dirtyY0 = y0;
        record->dirtyX1 = x1;
        record->dirtyY1 = y1;
        return;
    }
    record->dirtyX0 = x0 < record->dirtyX0 ? x0 : record->dirtyX0;
    record->dirtyY0 = y0 < record->dirtyY0 ? y0 : record->dirtyY0;
    record->dirtyX1 = x1 > record->dirtyX1 ? x1 : record->dirtyX1;
    record->dirtyY1 = y1 > record->dirtyY1 ? y1 : record->dirtyY1;
}

// The form of a key: its size in 64ths of a pixel and, for coverage,
// its quarter-pixel position, or, for a field, its spread.
static uint32_t FormOf(uint32_t size, uint32_t quarter, uint32_t spread)
{
    return spread << SPREAD_SHIFT | size << 2 | quarter;
}

// A colour packed as 8-bit sRGB red, green, blue and straight alpha, from
// the high byte, and back as premultiplied linear colour.
static uint32_t PackTint(muiLinearColor color)
{
    float alpha = fminf(fmaxf(color.a, 0.0f), 1.0f);
    float channels[3] = {color.r, color.g, color.b};
    uint32_t packed = 0;
    for (int i = 0; i < 3; i++)
    {
        float straight = alpha > 0.0f ? channels[i] / alpha : 0.0f;
        packed = packed << 8 | (uint32_t)(muiEncodeSrgb(straight) * 255.0f + 0.5f);
    }
    return packed << 8 | (uint32_t)(alpha * 255.0f + 0.5f);
}

static muiLinearColor UnpackTint(uint32_t packed)
{
    const muiColor color = {(float)(packed >> 24) / 255.0f, (float)(packed >> 16 & 0xFF) / 255.0f,
                            (float)(packed >> 8 & 0xFF) / 255.0f, (float)(packed & 0xFF) / 255.0f};
    double rgb[3];
    muiColorToLinearRgb(color, rgb);
    return muiPremultiply(rgb, color.a, 1.0f);
}

// Renders the image a key names into the scratch, as the atlas's format
// asks.
static muiResult Render(muiGlyphAtlas* atlas, const muiGlyphKey* key, muiGlyphImage* imageOut)
{
    float size = (float)((key->sizeBin & SIZE_MASK) >> 2) / 64.0f;
    float offset = (float)(key->sizeBin & 3u) / 4.0f;
    uint32_t spread = key->sizeBin >> SPREAD_SHIFT;
    size_t capacity = (size_t)atlas->plotWidth * atlas->plotHeight * atlas->channels;
    switch (atlas->format)
    {
    case mui_atlasColor:
        return muiRenderColorGlyph(atlas->service, key->font, key->glyph, size, offset,
                                   key->palette, UnpackTint(key->tint), imageOut, atlas->scratch,
                                   capacity);
    case mui_atlasFourChannel:
        return muiRenderGlyphMultiField(atlas->service, key->font, key->glyph, size, spread,
                                        imageOut, atlas->scratch, capacity);
    default:
        return spread != 0 ? muiRenderGlyphField(atlas->service, key->font, key->glyph, size,
                                                 spread, imageOut, atlas->scratch, capacity)
                           : muiRenderGlyph(atlas->service, key->font, key->glyph, size, offset,
                                            imageOut, atlas->scratch, capacity);
    }
}

// Renders a glyph and packs it; the entry for key is set and given.
static muiResult Add(muiGlyphAtlas* atlas, const muiGlyphKey* key, muiAtlasEntry** entryOut,
                     muiAtlasGlyph* glyphOut)
{
    muiGlyphImage image = {0, 0, 0, 0};
    // A glyph without colour layers is empty, to be drawn as coverage.
    muiResult result = Render(atlas, key, &image);
    bool tooLarge =
        image.width + GUTTER > atlas->plotWidth || image.height + GUTTER > atlas->plotHeight;
    if ((result == mui_success || result == mui_errorCapacity) && tooLarge)
    {
        glyphOut->width = image.width;
        glyphOut->height = image.height;
        return mui_errorCapacity;
    }
    if (result != mui_success)
    {
        return result;
    }
    if (!muiReserveEntry(&atlas->allocator, &atlas->table, atlas->generations))
    {
        return mui_errorCapacity;
    }
    muiAtlasEntry placed = {
        .key = *key,
        .plot = MUI_NO_PLOT,
        .width = (uint16_t)image.width,
        .height = (uint16_t)image.height,
        .left = image.left,
        .top = image.top,
    };
    if (image.width != 0)
    {
        uint32_t plot = 0;
        uint32_t x = 0;
        uint32_t y = 0;
        result = Pack(atlas, image.width + GUTTER, image.height + GUTTER, &plot, &x, &y);
        if (result != mui_success)
        {
            return result;
        }
        unsigned char* pixels = PageOf(atlas, plot);
        size_t rowBytes = (size_t)image.width * atlas->channels;
        for (uint32_t row = 0; row < image.height; row++)
        {
            memcpy(pixels + ((size_t)(y + 1 + row) * atlas->pageWidth + x + 1) * atlas->channels,
                   atlas->scratch + (size_t)row * rowBytes, rowBytes);
        }
        MarkChanged(atlas, plot, x, y, image.width + GUTTER, image.height + GUTTER);
        placed.plot = plot;
        placed.generation = atlas->generations[plot - 1];
        placed.u = (uint16_t)(x + 1);
        placed.v = (uint16_t)(y + 1);
    }
    muiAtlasEntry* entry = muiFindEntry(&atlas->table, key);
    atlas->table.count += entry->plot == 0 ? 1u : 0u;
    *entry = placed;
    *entryOut = entry;
    return mui_success;
}

// The largest integer not above value, which is within 2^62 of 0;
// rounding inline, as the C library's functions are calls.
static int64_t FloorToInteger(float value)
{
    int64_t truncated = (int64_t)value;
    return truncated - ((float)truncated > value ? 1 : 0);
}

// Finds a glyph of a form, rendering and packing it the first time, and
// keeps its plot until a later frame; glyphOut is given its page and
// place in it.
// Finds a glyph's entry, adding it the first time: mui_empty, with no
// entry, for a glyph without colour layers in an atlas of colour glyphs,
// found again each time (a search of the COLR table's base glyphs).
static muiResult Find(muiGlyphAtlas* atlas, uint64_t font, uint32_t glyph, uint32_t form,
                      uint32_t palette, uint32_t tint, const muiAtlasEntry** entryOut,
                      muiAtlasGlyph* glyphOut)
{
    uint64_t key = 0;
    const muiFont* record = muiFindFont(atlas->service, font, &key);
    if (record == nullptr)
    {
        return mui_errorStale;
    }
    if (glyph >= record->metrics.glyphCount)
    {
        return muiRefuseText(atlas->service);
    }
    *glyphOut = (muiAtlasGlyph){0, 0, 0, 0, 0, 0, 0};
    if (atlas->format == mui_atlasColor && !record->colorLayers && !muiHasColorBitmaps(record))
    {
        return mui_empty;
    }
    muiGlyphKey lookup = {key, glyph, form, palette, tint};
    muiAtlasEntry* entry =
        atlas->table.capacity != 0 ? muiFindEntry(&atlas->table, &lookup) : nullptr;
    if (entry == nullptr || entry->plot == 0 || muiIsEntryStale(entry, atlas->generations))
    {
        muiResult result = Add(atlas, &lookup, &entry, glyphOut);
        if (result != mui_success)
        {
            return result;
        }
    }
    if (entry->plot != MUI_NO_PLOT)
    {
        Plot* plot = &atlas->plots[entry->plot - 1];
        plot->lastUse = atlas->frame;
        glyphOut->page = plot->page;
    }
    glyphOut->u = entry->u;
    glyphOut->v = entry->v;
    glyphOut->width = entry->width;
    glyphOut->height = entry->height;
    *entryOut = entry;
    return mui_success;
}

static bool IsSizeValid(float pixelSize)
{
    return pixelSize >= 1.0f / 64.0f && pixelSize <= MUI_MAX_GLYPH_PIXEL_SIZE;
}

// The size as the renderers round it.
static uint32_t SizeOf(float pixelSize)
{
    return (uint32_t)(pixelSize * 64.0f + 0.5f);
}

// Gets an image for a pen in device pixels: coverage, or a colour glyph
// in a palette and a text colour packed.
static muiResult GetAt(muiGlyphAtlas* atlas, uint64_t font, uint32_t glyph, float pixelSize,
                       float penX, float baselineY, uint32_t palette, uint32_t tint,
                       muiAtlasGlyph* glyphOut)
{
    if (glyphOut == nullptr || !IsSizeValid(pixelSize) || !(fabsf(penX) <= MAX_PEN) ||
        !(fabsf(baselineY) <= MAX_PEN))
    {
        return muiRefuseText(atlas->service);
    }
    // The pen to the nearest quarter pixel.
    int64_t quarters = FloorToInteger(penX * 4.0f + 0.5f);
    int64_t bin = quarters & 3;
    int64_t pen = (quarters - bin) / 4;
    const muiAtlasEntry* entry = nullptr;
    muiResult result = Find(atlas, font, glyph, FormOf(SizeOf(pixelSize), (uint32_t)bin, 0),
                            palette, tint, &entry, glyphOut);
    if (result == mui_success)
    {
        glyphOut->x = (int32_t)pen + entry->left;
        glyphOut->y = (int32_t)FloorToInteger(baselineY + 0.5f) - entry->top;
    }
    return result;
}

muiResult muiGlyphAtlas_Get(muiGlyphAtlas* atlas, uint64_t font, uint32_t glyph, float pixelSize,
                            float penX, float baselineY, muiAtlasGlyph* glyphOut)
{
    return atlas != nullptr && atlas->format == mui_atlasOneChannel
               ? GetAt(atlas, font, glyph, pixelSize, penX, baselineY, 0, 0, glyphOut)
               : muiRefuseText(atlas != nullptr ? atlas->service : nullptr);
}

muiResult muiGlyphAtlas_GetColor(muiGlyphAtlas* atlas, uint64_t font, uint32_t glyph,
                                 float pixelSize, float penX, float baselineY, uint32_t palette,
                                 muiLinearColor foreground, muiAtlasGlyph* glyphOut)
{
    return atlas != nullptr && atlas->format == mui_atlasColor
               ? GetAt(atlas, font, glyph, pixelSize, penX, baselineY, palette,
                       PackTint(foreground), glyphOut)
               : muiRefuseText(atlas != nullptr ? atlas->service : nullptr);
}

// Gets a field, of one channel or of four as the atlas is.
static muiResult GetField(muiGlyphAtlas* atlas, uint64_t font, uint32_t glyph, float pixelSize,
                          uint32_t spread, muiAtlasGlyph* glyphOut)
{
    if (glyphOut == nullptr || !IsSizeValid(pixelSize) || spread < MUI_MIN_FIELD_SPREAD ||
        spread > MUI_MAX_FIELD_SPREAD)
    {
        return muiRefuseText(atlas->service);
    }
    const muiAtlasEntry* entry = nullptr;
    muiResult result =
        Find(atlas, font, glyph, FormOf(SizeOf(pixelSize), 0, spread), 0, 0, &entry, glyphOut);
    if (result == mui_success)
    {
        glyphOut->x = entry->left;
        glyphOut->y = -entry->top;
    }
    return result;
}

muiResult muiGlyphAtlas_GetField(muiGlyphAtlas* atlas, uint64_t font, uint32_t glyph,
                                 float pixelSize, uint32_t spread, muiAtlasGlyph* glyphOut)
{
    return atlas != nullptr && atlas->format == mui_atlasOneChannel
               ? GetField(atlas, font, glyph, pixelSize, spread, glyphOut)
               : muiRefuseText(atlas != nullptr ? atlas->service : nullptr);
}

muiResult muiGlyphAtlas_GetMultiField(muiGlyphAtlas* atlas, uint64_t font, uint32_t glyph,
                                      float pixelSize, uint32_t spread, muiAtlasGlyph* glyphOut)
{
    return atlas != nullptr && atlas->format == mui_atlasFourChannel
               ? GetField(atlas, font, glyph, pixelSize, spread, glyphOut)
               : muiRefuseText(atlas != nullptr ? atlas->service : nullptr);
}

uint32_t muiGlyphAtlas_GetPageCount(const muiGlyphAtlas* atlas)
{
    return atlas != nullptr ? atlas->pageCount : 0;
}

muiResult muiGlyphAtlas_GetPage(const muiGlyphAtlas* atlas, uint32_t page, muiAtlasPage* pageOut)
{
    if (atlas == nullptr || pageOut == nullptr || page >= atlas->pageCount)
    {
        return mui_errorInvalid;
    }
    *pageOut =
        (muiAtlasPage){atlas->pages[page], atlas->pageWidth, atlas->pageHeight, atlas->format};
    return mui_success;
}

muiResult muiGlyphAtlas_TakeUpdates(muiGlyphAtlas* atlas, muiAtlasUpdate* updates,
                                    uint32_t capacity, uint32_t* countOut)
{
    if (atlas == nullptr || countOut == nullptr || (updates == nullptr && capacity != 0))
    {
        return muiRefuseText(atlas != nullptr ? atlas->service : nullptr);
    }
    *countOut = atlas->dirtyCount;
    if (capacity < atlas->dirtyCount)
    {
        return mui_errorCapacity;
    }
    for (uint32_t i = 0; i < atlas->dirtyCount; i++)
    {
        Plot* plot = &atlas->plots[atlas->dirty[i]];
        updates[i] = (muiAtlasUpdate){atlas->dirty[i] / atlas->plotsPerPage, plot->dirtyX0,
                                      plot->dirtyY0, (uint32_t)(plot->dirtyX1 - plot->dirtyX0),
                                      (uint32_t)(plot->dirtyY1 - plot->dirtyY0)};
        plot->listed = false;
    }
    atlas->dirtyCount = 0;
    return mui_success;
}
