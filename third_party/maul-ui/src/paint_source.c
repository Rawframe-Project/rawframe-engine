// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Palette colours for colour glyphs (record mui-0006): CPAL entries are
// sRGB with straight alpha, made linear and premultiplied here.

#include "paint_source.h"

#include "color.h"

muiLinearColor muiPaletteColor(const FT_Color* palette, uint32_t entries, uint32_t index,
                               muiLinearColor foreground)
{
    if (index == MUI_FOREGROUND_ENTRY)
    {
        return foreground;
    }
    if (palette == nullptr || index >= entries)
    {
        return (muiLinearColor){0.0f, 0.0f, 0.0f, 0.0f};
    }
    const FT_Color* entry = &palette[index];
    const muiColor color = {(float)entry->red / 255.0f, (float)entry->green / 255.0f,
                            (float)entry->blue / 255.0f, (float)entry->alpha / 255.0f};
    double rgb[3];
    muiColorToLinearRgb(color, rgb);
    return muiPremultiply(rgb, color.a, 1.0f);
}

#if MUI_COLR_PAINT

muiLinearColor muiColrPaintColor(const muiPaintSource* source, FT_ColorIndex index)
{
    float alpha = (float)index.alpha / 16384.0f;
    muiLinearColor color =
        muiPaletteColor(source->palette, source->entries, index.palette_index, source->foreground);
    return (muiLinearColor){color.r * alpha, color.g * alpha, color.b * alpha, color.a * alpha};
}

#endif
