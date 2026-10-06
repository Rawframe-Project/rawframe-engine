// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Building font chains (record mui-0006).

#include "font_chain.h"

#include "font_family.h"
#include "font_instance.h"

#include "maul-ui/text_block.h"

// The face a key names for a style: a font, key 0 the default font, or
// the face of a family the style's weight and slant choose; with its
// key, at instance 0. NULL when it names none.
static muiFont* FaceOf(const muiTextService* service, uint64_t key,
                       const muiComputedTextStyle* style, uint64_t* keyOut)
{
    if ((key & MUI_FAMILY_BIT) == 0)
    {
        return muiFindFont(service, key & MUI_FONT_PART_MASK, keyOut);
    }
    const muiFontFamily* family = muiFindFamily(service, key);
    muiFontId face = {0, 0};
    const muiFont* font = family != nullptr
                              ? muiMatchFamily(service, family, style->weight, style->slant, &face)
                              : nullptr;
    *keyOut = font != nullptr ? muiFont_GetKey(face) : 0;
    return font != nullptr ? &service->fonts.fonts[face.index1 - 1] : nullptr;
}

// Adds the face a key names, in the style's instance, unless the chain
// has it.
static void Add(const muiTextService* service, uint64_t key, const muiComputedTextStyle* style,
                muiFontChain* chain)
{
    uint64_t faceKey = 0;
    muiFont* font = FaceOf(service, key, style, &faceKey);
    if (font == nullptr)
    {
        return;
    }
    faceKey |= muiInstanceBits(font, style->weight, style->slant, style->size);
    for (uint32_t i = 0; i < chain->count; i++)
    {
        if (chain->keys[i] == faceKey)
        {
            return;
        }
    }
    chain->fonts[chain->count] = font;
    chain->keys[chain->count++] = faceKey;
    // FNV-1a over the keys.
    for (int byte = 0; byte < 8; byte++)
    {
        chain->identity = (chain->identity ^ ((faceKey >> (8 * byte)) & 0xFFu)) * 0x100000001B3u;
    }
}

bool muiBuildChain(const muiTextService* service, const muiComputedTextStyle* style,
                   muiFontChain* out)
{
    out->count = 0;
    out->identity = 0xCBF29CE484222325u;
    Add(service, style->font, style, out);
    if (out->count == 0)
    {
        return false;
    }
    const muiFontFamily* family = muiFindFamily(service, style->font);
    for (uint32_t i = 0; family != nullptr && i < family->fallbackCount; i++)
    {
        Add(service, family->fallbacks[i], style, out);
    }
    for (uint32_t i = 0; i < service->fallbackCount; i++)
    {
        Add(service, service->fallbacks[i], style, out);
    }
    return true;
}
