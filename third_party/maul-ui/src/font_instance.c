// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Instances as CSS makes them: wght from the weight, ital or slnt from
// the slant, opsz from the size, each within its axis's range; bold made
// when 600 or more is asked of a face that cannot reach 600, oblique when
// a slant is asked of an upright face without ital or slnt.

#include "font_instance.h"

enum
{
    GENERATION_BITS = 24,
    // The instance's fields, from the key's bit 40: the weight, the
    // slant's kind, the optical size in half units and the emboldening;
    // bit 63 is kept for families.
    WEIGHT_SHIFT = 40,
    SLANT_SHIFT = 50,
    SIZE_SHIFT = 52,
    BOLD_SHIFT = 62,
    WEIGHT_MASK = 1023,
    SIZE_MASK = 1023,
    // The slant's kinds.
    SLANT_NONE = 0,
    SLANT_ITALIC = 1,
    SLANT_SLANTED = 2,
    SLANT_SHEARED = 3,
    // Where synthesis makes bold, and the angle slnt is set to.
    BOLD_WEIGHT = 600,
    OBLIQUE_ANGLE = -14
};

static uint32_t Tag(char a, char b, char c, char d)
{
    return (uint32_t)(unsigned char)a << 24 | (uint32_t)(unsigned char)b << 16 |
           (uint32_t)(unsigned char)c << 8 | (uint32_t)(unsigned char)d;
}

uint32_t muiKeyGeneration(uint32_t generation)
{
    // Never 0, so no key but 0 names the default font.
    uint32_t kept = generation & ((1u << GENERATION_BITS) - 1);
    return kept != 0 ? kept : 1;
}

uint64_t muiKeyOf(uint32_t slot, uint32_t generation)
{
    return (uint64_t)muiKeyGeneration(generation) << 16 | (slot - 1);
}

static const muiFontAxis* FindAxis(const muiFont* font, uint32_t tag)
{
    for (uint32_t i = 0; i < font->axisCount; i++)
    {
        if (font->axes[i].tag == tag)
        {
            return &font->axes[i];
        }
    }
    return nullptr;
}

static float ToFloat(FT_Fixed value)
{
    return (float)value / 65536.0f;
}

static float Clamp(float value, const muiFontAxis* axis)
{
    float low = ToFloat(axis->minimum);
    float high = ToFloat(axis->maximum);
    return value < low ? low : (value > high ? high : value);
}

// What the slant asks of the font: an axis, a shear, or nothing.
static uint64_t SlantOf(const muiFont* font, muiFontSlant slant)
{
    if (slant == mui_slantNormal)
    {
        return SLANT_NONE;
    }
    bool ital = FindAxis(font, Tag('i', 't', 'a', 'l')) != nullptr;
    bool slnt = FindAxis(font, Tag('s', 'l', 'n', 't')) != nullptr;
    if (slant == mui_slantItalic && ital)
    {
        return SLANT_ITALIC;
    }
    if (slnt)
    {
        return SLANT_SLANTED;
    }
    if (ital)
    {
        return SLANT_ITALIC;
    }
    return font->faceSlant != mui_slantNormal ? SLANT_NONE : SLANT_SHEARED;
}

uint64_t muiInstanceBits(const muiFont* font, float weight, muiFontSlant slant, float size)
{
    uint64_t bits = 0;
    const muiFontAxis* wght = FindAxis(font, Tag('w', 'g', 'h', 't'));
    float reach = (float)font->weightClass;
    if (wght != nullptr)
    {
        float value = Clamp(weight, wght);
        value = value < 1.0f ? 1.0f : (value > 1000.0f ? 1000.0f : value);
        bits |= (uint64_t)(uint32_t)(value + 0.5f) << WEIGHT_SHIFT;
        reach = ToFloat(wght->maximum);
    }
    if (weight >= (float)BOLD_WEIGHT && reach < (float)BOLD_WEIGHT)
    {
        bits |= 1ull << BOLD_SHIFT;
    }
    bits |= SlantOf(font, slant) << SLANT_SHIFT;
    const muiFontAxis* opsz = FindAxis(font, Tag('o', 'p', 's', 'z'));
    if (opsz != nullptr && size > 0.0f)
    {
        float halves = Clamp(size, opsz) * 2.0f + 0.5f;
        halves = halves < 1.0f ? 1.0f : (halves > (float)SIZE_MASK ? (float)SIZE_MASK : halves);
        bits |= (uint64_t)(uint32_t)halves << SIZE_SHIFT;
    }
    return bits;
}

muiInstance muiDecodeInstance(uint64_t key)
{
    uint32_t slant = (uint32_t)(key >> SLANT_SHIFT) & 3u;
    return (muiInstance){
        .weight = (float)((key >> WEIGHT_SHIFT) & WEIGHT_MASK),
        .opticalSize = (float)((key >> SIZE_SHIFT) & SIZE_MASK) * 0.5f,
        .italic = slant == SLANT_ITALIC,
        .slanted = slant == SLANT_SLANTED,
        .sheared = slant == SLANT_SHEARED,
        .emboldened = ((key >> BOLD_SHIFT) & 1u) != 0,
    };
}

static FT_Fixed ToFixed(float value)
{
    return (FT_Fixed)(value * 65536.0f);
}

uint32_t muiInstanceCoordinates(const muiFont* font, const muiInstance* instance,
                                FT_Fixed* coordinates)
{
    for (uint32_t i = 0; i < font->axisCount; i++)
    {
        const muiFontAxis* axis = &font->axes[i];
        FT_Fixed value = axis->defaultValue;
        if (axis->tag == Tag('w', 'g', 'h', 't') && instance->weight > 0.0f)
        {
            value = ToFixed(Clamp(instance->weight, axis));
        }
        else if (axis->tag == Tag('i', 't', 'a', 'l') && instance->italic)
        {
            value = ToFixed(Clamp(1.0f, axis));
        }
        else if (axis->tag == Tag('s', 'l', 'n', 't') && instance->slanted)
        {
            value = ToFixed(Clamp((float)OBLIQUE_ANGLE, axis));
        }
        else if (axis->tag == Tag('o', 'p', 's', 'z') && instance->opticalSize > 0.0f)
        {
            value = ToFixed(Clamp(instance->opticalSize, axis));
        }
        coordinates[i] = value;
    }
    return font->axisCount;
}

// Makes the HarfBuzz font of an instance other than 0.
static hb_font_t* MakeShaper(muiFont* font, uint64_t key)
{
    // A font of its own, at the face's units per em as HarfBuzz makes it:
    // a sub-font would ask its parent, at the default instance, for
    // advances.
    hb_font_t* shaper = hb_font_create(font->shapingFace);
    if (shaper == hb_font_get_empty())
    {
        return nullptr;
    }
    muiInstance instance = muiDecodeInstance(key);
    FT_Fixed fixed[MUI_MAX_FONT_AXES];
    float coordinates[MUI_MAX_FONT_AXES];
    uint32_t count = muiInstanceCoordinates(font, &instance, fixed);
    for (uint32_t i = 0; i < count; i++)
    {
        coordinates[i] = ToFloat(fixed[i]);
    }
    if (count != 0)
    {
        hb_font_set_var_coords_design(shaper, coordinates, count);
    }
    if (instance.emboldened)
    {
        hb_font_set_synthetic_bold(shaper, 1.0f / 24.0f, 1.0f / 24.0f, false);
    }
    if (instance.sheared)
    {
        hb_font_set_synthetic_slant(shaper, 0.25f);
    }
    hb_font_make_immutable(shaper);
    return shaper;
}

hb_font_t* muiShapingFontOf(muiFont* font, uint64_t key)
{
    uint64_t instance = key & ~MUI_FONT_PART_MASK;
    if (instance == 0)
    {
        return font->shapingFont;
    }
    for (uint32_t i = 0; i < MUI_SHAPING_SLOTS; i++)
    {
        if (font->shapers[i] != nullptr && font->shaperInstances[i] == instance)
        {
            return font->shapers[i];
        }
    }
    hb_font_t* shaper = MakeShaper(font, key);
    if (shaper == nullptr)
    {
        return nullptr;
    }
    uint32_t slot = font->nextShaper;
    font->nextShaper = (slot + 1) % MUI_SHAPING_SLOTS;
    if (font->shapers[slot] != nullptr)
    {
        hb_font_destroy(font->shapers[slot]);
    }
    font->shapers[slot] = shaper;
    font->shaperInstances[slot] = instance;
    return shaper;
}
