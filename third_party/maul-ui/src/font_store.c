// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Font records (record mui-0006).

#include "font_store.h"

#include "allocator.h"

void muiReleaseFont(const muiAllocator* allocator, muiFont* font)
{
    if (font->shapingFont != nullptr)
    {
        hb_font_destroy(font->shapingFont);
    }
    if (font->shapingFace != nullptr)
    {
        hb_face_destroy(font->shapingFace);
    }
    if (font->face != nullptr)
    {
        (void)FT_Done_Face(font->face);
    }
    if (font->copy != nullptr)
    {
        muiRelease(allocator, font->copy, font->size, 1);
    }
    *font = (muiFont){0};
}
