// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Glyph images (record mui-0006): FreeType loads a glyph's outline at a
// size, unhinted and without the font's embedded bitmaps; its smooth
// rasterizer renders coverage straight into the caller's bytes, and
// distance fields are drawn from the outline cut into segments.

#include "maul-ui/glyph_image.h"

#include "distance_field.h"
#include "flatten.h"
#include "glyph_outline.h"
#include "multi_field.h"
#include "text_service.h"

#include <math.h>
#include <stddef.h>
#include <string.h>

muiResult muiRenderGlyph(muiTextService* service, uint64_t font, uint32_t glyph, float pixelSize,
                         float offsetX, muiGlyphImage* imageOut, unsigned char* pixels,
                         size_t capacity)
{
    if (service == nullptr || imageOut == nullptr || (pixels == nullptr && capacity != 0) ||
        !muiIsGlyphSizeValid(pixelSize) || !(offsetX >= 0.0f && offsetX < 1.0f))
    {
        return muiRefuseText(service);
    }
    muiResult result = mui_success;
    uint64_t key = 0;
    muiFont* record = muiGlyphFontOf(service, font, glyph, &key, &result);
    if (record == nullptr)
    {
        return result;
    }
    *imageOut = (muiGlyphImage){0, 0, 0, 0};
    result = muiLoadGlyphOutline(record, key, glyph, lroundf(pixelSize * 64.0f),
                                 (FT_Pos)lroundf(offsetX * 64.0f));
    if (result != mui_success)
    {
        // A bitmap-only font's glyphs have no coverage.
        return result == mui_empty ? mui_success : result;
    }
    FT_Outline* outline = &record->face->glyph->outline;
    muiPixelBox box = muiOutlineBox(outline);
    if (box.width > MUI_MAX_IMAGE_EXTENT || box.height > MUI_MAX_IMAGE_EXTENT)
    {
        return mui_errorFormat;
    }
    *imageOut = (muiGlyphImage){(int32_t)box.left, (int32_t)(box.bottom + box.height),
                                (uint32_t)box.width, (uint32_t)box.height};
    size_t bytes = (size_t)box.width * (size_t)box.height;
    if (bytes > capacity)
    {
        return mui_errorCapacity;
    }
    if (bytes == 0)
    {
        return mui_success;
    }
    // The rasterizer writes only what the outline covers.
    memset(pixels, 0, bytes);
    return muiRasterizeOutline(service, outline, &box, pixels, (int)box.width);
}

// Cuts the loaded outline into segments, then pieces, and reserves the
// field's work memory, all in the service's memory.
// Reserves a multi-channel field's work memory beside the one-channel
// field's, for room edge segments.
static bool PrepareMulti(muiTextService* service, const muiFieldGrid* grid, size_t room,
                         muiFieldScratch* scratch, muiMultiScratch* multi)
{
    const muiAllocator* allocator = &service->allocator;
    size_t pixels = (size_t)grid->width * grid->height;
    if (!muiReserve(allocator, &service->fieldEdgeOrigins, room * sizeof(uint32_t)) ||
        !muiReserve(allocator, &service->fieldEdgeSides, room) ||
        !muiReserve(allocator, &service->fieldEdgeColors, room) ||
        !muiReserve(allocator, &service->fieldLoops, (room * 3 + 1) * sizeof(uint32_t)) ||
        !muiReserve(allocator, &service->fieldChannels, pixels * 9 * sizeof(float)) ||
        !muiReserve(allocator, &service->fieldInside, pixels))
    {
        return false;
    }
    scratch->edgeOrigins = service->fieldEdgeOrigins.data;
    scratch->edgeSides = service->fieldEdgeSides.data;
    uint32_t* loops = service->fieldLoops.data;
    *multi = (muiMultiScratch){
        service->fieldCurves.data, service->fieldEdgeColors.data, loops,
        loops + room + 1,          loops + room * 2 + 1,          service->fieldChannels.data,
        service->fieldInside.data};
    return true;
}

// Flattens an outline and reserves a field's work memory: a multi-channel
// field's too, when multi is not NULL.
static muiResult Prepare(muiTextService* service, const FT_Outline* outline,
                         const muiFieldGrid* grid, muiFieldScratch* scratch, muiMultiScratch* multi,
                         uint32_t* piecesOut)
{
    const muiAllocator* allocator = &service->allocator;
    uint32_t count = 0;
    if (!muiFlattenOutline(outline, nullptr, nullptr, 0, &count))
    {
        return mui_errorFormat;
    }
    if (!muiReserve(allocator, &service->fieldSegments, ((size_t)count + 1) * sizeof(muiSegment)) ||
        (multi != nullptr &&
         !muiReserve(allocator, &service->fieldCurves, ((size_t)count + 1) * sizeof(uint32_t))))
    {
        return mui_errorCapacity;
    }
    muiSegment* segments = service->fieldSegments.data;
    (void)muiFlattenOutline(outline, segments,
                            multi != nullptr ? service->fieldCurves.data : nullptr, count, &count);
    size_t pieces = muiCountPieces(segments, count);
    if (pieces > UINT32_MAX - 1 ||
        !muiReserve(allocator, &service->fieldPieces, (pieces + 1) * sizeof(muiSegment)) ||
        !muiReserve(allocator, &service->fieldOrigins, (pieces + 1) * sizeof(uint32_t)) ||
        !muiReserve(allocator, &service->fieldCellPieces, (pieces + 1) * sizeof(uint32_t)) ||
        !muiReserve(allocator, &service->fieldEdge,
                    muiEdgeRoom((uint32_t)pieces) * sizeof(muiSegment)))
    {
        return mui_errorCapacity;
    }
    uint32_t cut =
        muiCutPieces(segments, count, service->fieldPieces.data, service->fieldOrigins.data);
    size_t crossings = muiCountCrossings(service->fieldPieces.data, cut, grid);
    size_t pixels = (size_t)grid->width * grid->height;
    if (!muiReserve(allocator, &service->fieldRows,
                    ((size_t)grid->height + 1) * sizeof(uint32_t)) ||
        !muiReserve(allocator, &service->fieldCrossings, (crossings + 1) * sizeof(muiCrossing)) ||
        !muiReserve(allocator, &service->fieldCells, (pixels + 1) * sizeof(uint32_t)) ||
        !muiReserve(allocator, &service->fieldDistances, pixels * sizeof(float)))
    {
        return mui_errorCapacity;
    }
    *scratch = (muiFieldScratch){.rowStarts = service->fieldRows.data,
                                 .crossings = service->fieldCrossings.data,
                                 .cellStarts = service->fieldCells.data,
                                 .cellPieces = service->fieldCellPieces.data,
                                 .edge = service->fieldEdge.data,
                                 .distances = service->fieldDistances.data};
    if (multi != nullptr && !PrepareMulti(service, grid, muiEdgeRoom(cut), scratch, multi))
    {
        return mui_errorCapacity;
    }
    *piecesOut = cut;
    return mui_success;
}

// Renders a glyph's field of one channel, or of four when multi.
static muiResult RenderField(muiTextService* service, uint64_t font, uint32_t glyph,
                             float pixelSize, uint32_t spread, muiGlyphImage* imageOut,
                             unsigned char* pixels, size_t capacity, bool multi)
{
    if (service == nullptr || imageOut == nullptr || (pixels == nullptr && capacity != 0) ||
        !muiIsGlyphSizeValid(pixelSize) || spread < MUI_MIN_FIELD_SPREAD ||
        spread > MUI_MAX_FIELD_SPREAD)
    {
        return muiRefuseText(service);
    }
    muiResult result = mui_success;
    uint64_t key = 0;
    muiFont* record = muiGlyphFontOf(service, font, glyph, &key, &result);
    if (record == nullptr)
    {
        return result;
    }
    *imageOut = (muiGlyphImage){0, 0, 0, 0};
    result = muiLoadGlyphOutline(record, key, glyph, lroundf(pixelSize * 64.0f), 0);
    if (result != mui_success)
    {
        // A bitmap-only font's glyphs have no field.
        return result == mui_empty ? mui_success : result;
    }
    const FT_Outline* outline = &record->face->glyph->outline;
    if (outline->n_points == 0)
    {
        return mui_success;
    }
    muiPixelBox box = muiOutlineBox(outline);
    FT_Pos reach = (FT_Pos)spread;
    FT_Pos left = box.left - reach;
    FT_Pos bottom = box.bottom - reach;
    FT_Pos width = box.width + 2 * reach;
    FT_Pos height = box.height + 2 * reach;
    if (width > MUI_MAX_IMAGE_EXTENT || height > MUI_MAX_IMAGE_EXTENT)
    {
        return mui_errorFormat;
    }
    *imageOut = (muiGlyphImage){(int32_t)left, (int32_t)(bottom + height), (uint32_t)width,
                                (uint32_t)height};
    if ((size_t)width * (size_t)height * (multi ? 4u : 1u) > capacity)
    {
        return mui_errorCapacity;
    }
    muiFieldGrid grid = {(int32_t)left,   (int32_t)(bottom + height),
                         (uint32_t)width, (uint32_t)height,
                         spread,          (outline->flags & FT_OUTLINE_EVEN_ODD_FILL) != 0};
    muiFieldScratch scratch;
    muiMultiScratch multiScratch;
    uint32_t pieces = 0;
    result = Prepare(service, outline, &grid, &scratch, multi ? &multiScratch : nullptr, &pieces);
    if (result != mui_success)
    {
        return result;
    }
    if (multi)
    {
        muiDrawMultiField(service->fieldPieces.data, service->fieldOrigins.data, pieces, &grid,
                          &scratch, &multiScratch, pixels);
    }
    else
    {
        muiDrawDistanceField(service->fieldPieces.data, service->fieldOrigins.data, pieces, &grid,
                             &scratch, pixels);
    }
    return mui_success;
}

muiResult muiRenderGlyphField(muiTextService* service, uint64_t font, uint32_t glyph,
                              float pixelSize, uint32_t spread, muiGlyphImage* imageOut,
                              unsigned char* pixels, size_t capacity)
{
    return RenderField(service, font, glyph, pixelSize, spread, imageOut, pixels, capacity, false);
}

muiResult muiRenderGlyphMultiField(muiTextService* service, uint64_t font, uint32_t glyph,
                                   float pixelSize, uint32_t spread, muiGlyphImage* imageOut,
                                   unsigned char* pixels, size_t capacity)
{
    return RenderField(service, font, glyph, pixelSize, spread, imageOut, pixels, capacity, true);
}
