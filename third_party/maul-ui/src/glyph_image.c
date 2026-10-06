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
#include "font_instance.h"
#include "text_service.h"

#include FT_MULTIPLE_MASTERS_H
#include FT_OUTLINE_H

#include <math.h>
#include <stddef.h>
#include <string.h>

enum
{
    // The widest and tallest image rendered, in pixels: FreeType's
    // rasterizer works in 16-bit pixel coordinates.
    MAX_IMAGE_EXTENT = 32767
};

static muiResult Failed(FT_Error error)
{
    return FT_ERROR_BASE(error) == FT_Err_Out_Of_Memory ? mui_errorCapacity : mui_errorFormat;
}

static FT_Pos FloorPixel(FT_Pos value)
{
    return value >= 0 ? value / 64 : -((-value + 63) / 64);
}

static FT_Pos CeilPixel(FT_Pos value)
{
    return -FloorPixel(-value);
}

// Sets a font's face to the instance a key names, unless it is set so.
static muiResult SetInstance(muiFont* font, uint64_t key)
{
    uint64_t instance = key & ~MUI_FONT_PART_MASK;
    if (font->axisCount == 0 || font->imageInstance == instance)
    {
        return mui_success;
    }
    muiInstance decoded = muiDecodeInstance(key);
    FT_Fixed coordinates[MUI_MAX_FONT_AXES];
    uint32_t count = muiInstanceCoordinates(font, &decoded, coordinates);
    FT_Error error = FT_Set_Var_Design_Coordinates(font->face, count, coordinates);
    if (error != 0)
    {
        // No instance has every bit set: set again next time.
        font->imageInstance = UINT64_MAX;
        return Failed(error);
    }
    font->imageInstance = instance;
    return mui_success;
}

// Makes the oblique and the bold an instance asks for, as HarfBuzz does
// for shaping: sheared by a quarter of the height, then grown by an em/24
// up and right, FreeType keeping the left side bearing.
static muiResult Synthesize(FT_Outline* outline, uint64_t key, long size)
{
    muiInstance instance = muiDecodeInstance(key);
    if (instance.sheared)
    {
        FT_Matrix shear = {0x10000, 0x4000, 0, 0x10000};
        FT_Outline_Transform(outline, &shear);
    }
    if (instance.emboldened)
    {
        FT_Pos strength = size / 24;
        FT_Error error = FT_Outline_EmboldenXY(outline, strength, strength);
        if (error != 0)
        {
            return Failed(error);
        }
    }
    return mui_success;
}

// Loads a glyph's outline in a key's instance at a size in 64ths of a
// pixel, the pen moved right by offset 64ths.
static muiResult LoadOutline(muiFont* font, uint64_t key, uint32_t glyph, long size, FT_Pos offset)
{
    FT_Face face = font->face;
    muiResult result = SetInstance(font, key);
    if (result != mui_success)
    {
        return result;
    }
    if (font->imageSize != size)
    {
        FT_Error error = FT_Set_Char_Size(face, 0, size, 72, 72);
        if (error != 0)
        {
            font->imageSize = 0;
            return Failed(error);
        }
        font->imageSize = size;
    }
    FT_Error error = FT_Load_Glyph(face, glyph, FT_LOAD_NO_HINTING | FT_LOAD_NO_BITMAP);
    if (error != 0)
    {
        return Failed(error);
    }
    if (face->glyph->format != FT_GLYPH_FORMAT_OUTLINE)
    {
        return mui_errorFormat;
    }
    result = Synthesize(&face->glyph->outline, key, size);
    FT_Outline_Translate(&face->glyph->outline, offset, 0);
    return result;
}

// The font of a key, checked to have the glyph: NULL with the result
// otherwise.
static muiFont* FontOf(const muiTextService* service, uint64_t font, uint32_t glyph,
                       uint64_t* keyOut, muiResult* result)
{
    muiFont* record = muiFindFont(service, font, keyOut);
    *result = record == nullptr                     ? mui_errorStale
              : glyph >= record->metrics.glyphCount ? mui_errorInvalid
                                                    : mui_success;
    return *result == mui_success ? record : nullptr;
}

static bool IsSizeValid(float pixelSize)
{
    return pixelSize >= 1.0f / 64.0f && pixelSize <= MUI_MAX_GLYPH_PIXEL_SIZE;
}

muiResult muiRenderGlyph(muiTextService* service, uint64_t font, uint32_t glyph, float pixelSize,
                         float offsetX, muiGlyphImage* imageOut, unsigned char* pixels,
                         size_t capacity)
{
    if (service == nullptr || imageOut == nullptr || (pixels == nullptr && capacity != 0) ||
        !IsSizeValid(pixelSize) || !(offsetX >= 0.0f && offsetX < 1.0f))
    {
        return mui_errorInvalid;
    }
    muiResult result = mui_success;
    uint64_t key = 0;
    muiFont* record = FontOf(service, font, glyph, &key, &result);
    if (record == nullptr)
    {
        return result;
    }
    result = LoadOutline(record, key, glyph, lroundf(pixelSize * 64.0f),
                         (FT_Pos)lroundf(offsetX * 64.0f));
    if (result != mui_success)
    {
        return result;
    }
    FT_Outline* outline = &record->face->glyph->outline;
    // An empty outline's box is all 0.
    FT_BBox box;
    FT_Outline_Get_CBox(outline, &box);
    FT_Pos left = FloorPixel(box.xMin);
    FT_Pos bottom = FloorPixel(box.yMin);
    FT_Pos width = CeilPixel(box.xMax) - left;
    FT_Pos height = CeilPixel(box.yMax) - bottom;
    if (width > MAX_IMAGE_EXTENT || height > MAX_IMAGE_EXTENT)
    {
        return mui_errorFormat;
    }
    *imageOut = (muiGlyphImage){(int32_t)left, (int32_t)(bottom + height), (uint32_t)width,
                                (uint32_t)height};
    size_t bytes = (size_t)width * (size_t)height;
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
    FT_Outline_Translate(outline, -left * 64, -bottom * 64);
    FT_Bitmap bitmap = {
        .rows = (unsigned int)height,
        .width = (unsigned int)width,
        .pitch = (int)width,
        .buffer = pixels,
        .num_grays = 256,
        .pixel_mode = FT_PIXEL_MODE_GRAY,
    };
    FT_Error error = FT_Outline_Get_Bitmap(service->freetype, outline, &bitmap);
    return error == 0 ? mui_success : Failed(error);
}

// Cuts the loaded outline into segments, then pieces, and reserves the
// field's work memory, all in the service's memory.
static muiResult Prepare(muiTextService* service, const FT_Outline* outline,
                         const muiFieldGrid* grid, muiFieldScratch* scratch, uint32_t* piecesOut)
{
    const muiAllocator* allocator = &service->allocator;
    uint32_t count = 0;
    if (!muiFlattenOutline(outline, nullptr, 0, &count))
    {
        return mui_errorFormat;
    }
    if (!muiReserve(allocator, &service->fieldSegments, ((size_t)count + 1) * sizeof(muiSegment)))
    {
        return mui_errorCapacity;
    }
    muiSegment* segments = service->fieldSegments.data;
    (void)muiFlattenOutline(outline, segments, count, &count);
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
    *scratch = (muiFieldScratch){service->fieldRows.data,  service->fieldCrossings.data,
                                 service->fieldCells.data, service->fieldCellPieces.data,
                                 service->fieldEdge.data,  service->fieldDistances.data};
    *piecesOut = cut;
    return mui_success;
}

muiResult muiRenderGlyphField(muiTextService* service, uint64_t font, uint32_t glyph,
                              float pixelSize, uint32_t spread, muiGlyphImage* imageOut,
                              unsigned char* pixels, size_t capacity)
{
    if (service == nullptr || imageOut == nullptr || (pixels == nullptr && capacity != 0) ||
        !IsSizeValid(pixelSize) || spread < MUI_MIN_FIELD_SPREAD || spread > MUI_MAX_FIELD_SPREAD)
    {
        return mui_errorInvalid;
    }
    muiResult result = mui_success;
    uint64_t key = 0;
    muiFont* record = FontOf(service, font, glyph, &key, &result);
    if (record == nullptr)
    {
        return result;
    }
    result = LoadOutline(record, key, glyph, lroundf(pixelSize * 64.0f), 0);
    if (result != mui_success)
    {
        return result;
    }
    const FT_Outline* outline = &record->face->glyph->outline;
    *imageOut = (muiGlyphImage){0, 0, 0, 0};
    if (outline->n_points == 0)
    {
        return mui_success;
    }
    FT_BBox box;
    FT_Outline_Get_CBox(outline, &box);
    FT_Pos reach = (FT_Pos)spread;
    FT_Pos left = FloorPixel(box.xMin) - reach;
    FT_Pos bottom = FloorPixel(box.yMin) - reach;
    FT_Pos width = CeilPixel(box.xMax) + reach - left;
    FT_Pos height = CeilPixel(box.yMax) + reach - bottom;
    if (width > MAX_IMAGE_EXTENT || height > MAX_IMAGE_EXTENT)
    {
        return mui_errorFormat;
    }
    *imageOut = (muiGlyphImage){(int32_t)left, (int32_t)(bottom + height), (uint32_t)width,
                                (uint32_t)height};
    if ((size_t)width * (size_t)height > capacity)
    {
        return mui_errorCapacity;
    }
    muiFieldGrid grid = {(int32_t)left,   (int32_t)(bottom + height),
                         (uint32_t)width, (uint32_t)height,
                         spread,          (outline->flags & FT_OUTLINE_EVEN_ODD_FILL) != 0};
    muiFieldScratch scratch;
    uint32_t pieces = 0;
    result = Prepare(service, outline, &grid, &scratch, &pieces);
    if (result != mui_success)
    {
        return result;
    }
    muiDrawDistanceField(service->fieldPieces.data, service->fieldOrigins.data, pieces, &grid,
                         &scratch, pixels);
    return mui_success;
}
