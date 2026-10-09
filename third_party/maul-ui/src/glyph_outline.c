// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Glyph outlines for images (record mui-0006): FreeType loads a glyph's
// outline at a size, unhinted and without the font's embedded bitmaps,
// and its smooth rasterizer renders coverage.

#include "glyph_outline.h"

#include "font_instance.h"

#include "maul-ui/glyph_image.h"

#include FT_MULTIPLE_MASTERS_H
#include FT_OUTLINE_H

muiResult muiGlyphFailed(FT_Error error)
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
        return muiGlyphFailed(error);
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
            return muiGlyphFailed(error);
        }
    }
    return mui_success;
}

muiResult muiLoadGlyphOutline(muiFont* font, uint64_t key, uint32_t glyph, long size, FT_Pos offset)
{
    FT_Face face = font->face;
    if (!FT_IS_SCALABLE(face))
    {
        return mui_empty;
    }
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
            return muiGlyphFailed(error);
        }
        font->imageSize = size;
    }
    FT_Error error = FT_Load_Glyph(face, glyph, FT_LOAD_NO_HINTING | FT_LOAD_NO_BITMAP);
    if (error != 0)
    {
        return muiGlyphFailed(error);
    }
    if (face->glyph->format != FT_GLYPH_FORMAT_OUTLINE)
    {
        return mui_errorFormat;
    }
    result = Synthesize(&face->glyph->outline, key, size);
    FT_Outline_Translate(&face->glyph->outline, offset, 0);
    return result;
}

muiFont* muiGlyphFontOf(const muiTextService* service, uint64_t font, uint32_t glyph,
                        uint64_t* keyOut, muiResult* result)
{
    muiFont* record = muiFindFont(service, font, keyOut);
    *result = record == nullptr                     ? mui_errorStale
              : glyph >= record->metrics.glyphCount ? mui_errorInvalid
                                                    : mui_success;
    return *result == mui_success ? record : nullptr;
}

bool muiIsGlyphSizeValid(float pixelSize)
{
    return pixelSize >= 1.0f / 64.0f && pixelSize <= MUI_MAX_GLYPH_PIXEL_SIZE;
}

muiPixelBox muiPixelBoxOf(FT_BBox box)
{
    FT_Pos left = FloorPixel(box.xMin);
    FT_Pos bottom = FloorPixel(box.yMin);
    return (muiPixelBox){left, bottom, CeilPixel(box.xMax) - left, CeilPixel(box.yMax) - bottom};
}

muiPixelBox muiOutlineBox(const FT_Outline* outline)
{
    // An empty outline's box is all 0.
    FT_BBox box;
    FT_Outline_Get_CBox(outline, &box);
    return muiPixelBoxOf(box);
}

void muiJoinPixelBox(muiPixelBox* box, muiPixelBox own)
{
    if (own.width <= 0 || own.height <= 0)
    {
        return;
    }
    if (box->width <= 0 || box->height <= 0)
    {
        *box = own;
        return;
    }
    FT_Pos left = box->left < own.left ? box->left : own.left;
    FT_Pos bottom = box->bottom < own.bottom ? box->bottom : own.bottom;
    FT_Pos right = box->left + box->width;
    FT_Pos top = box->bottom + box->height;
    right = right > own.left + own.width ? right : own.left + own.width;
    top = top > own.bottom + own.height ? top : own.bottom + own.height;
    *box = (muiPixelBox){left, bottom, right - left, top - bottom};
}

muiResult muiRasterizeOutline(const muiTextService* service, FT_Outline* outline,
                              const muiPixelBox* box, unsigned char* pixels, int pitch)
{
    FT_Outline_Translate(outline, -box->left * 64, -box->bottom * 64);
    FT_Bitmap bitmap = {
        .rows = (unsigned int)box->height,
        .width = (unsigned int)box->width,
        .pitch = pitch,
        .buffer = pixels,
        .num_grays = 256,
        .pixel_mode = FT_PIXEL_MODE_GRAY,
    };
    FT_Error error = FT_Outline_Get_Bitmap(service->freetype, outline, &bitmap);
    return error == 0 ? mui_success : muiGlyphFailed(error);
}
