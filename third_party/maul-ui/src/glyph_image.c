// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Glyph images (record mui-0006): FreeType loads a glyph's outline at a
// size, unhinted and without the font's embedded bitmaps, and its smooth
// rasterizer renders it straight into the caller's bytes.

#include "maul-ui/glyph_image.h"

#include "text_service.h"

#include FT_OUTLINE_H

#include <math.h>
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

// Loads a glyph's outline at a size in 64ths of a pixel, the pen moved
// right by offset 64ths.
static muiResult LoadOutline(muiFont* font, uint32_t glyph, long size, FT_Pos offset)
{
    FT_Face face = font->face;
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
    FT_Outline_Translate(&face->glyph->outline, offset, 0);
    return mui_success;
}

muiResult muiRenderGlyph(muiTextService* service, uint64_t font, uint32_t glyph, float pixelSize,
                         float offsetX, muiGlyphImage* imageOut, unsigned char* pixels,
                         size_t capacity)
{
    if (service == nullptr || imageOut == nullptr || (pixels == nullptr && capacity != 0) ||
        !(pixelSize >= 1.0f / 64.0f && pixelSize <= MUI_MAX_GLYPH_PIXEL_SIZE) ||
        !(offsetX >= 0.0f && offsetX < 1.0f))
    {
        return mui_errorInvalid;
    }
    uint64_t key = 0;
    muiFont* record = muiFindFont(service, font, &key);
    if (record == nullptr)
    {
        return mui_errorStale;
    }
    if (glyph >= record->metrics.glyphCount)
    {
        return mui_errorInvalid;
    }
    muiResult result =
        LoadOutline(record, glyph, lroundf(pixelSize * 64.0f), (FT_Pos)lroundf(offsetX * 64.0f));
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
