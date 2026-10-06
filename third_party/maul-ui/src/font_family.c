// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Font families (record mui-0006): faces matched as CSS Fonts 4 matches
// them, narrowing by width (toward normal), then style, then weight, a
// variable face matching every value its axes reach.

#include "font_family.h"

#include "allocator.h"
#include "font_instance.h"
#include "text_service.h"

#include "maul-ui/text_block.h"

#include <stdalign.h>
#include <string.h>

#define FONT_FAMILY_DEF_COOKIE 0x6D756666u // "muff"

enum
{
    MAX_FACES = 256,
    // The width CSS asks for when none is given, in percent.
    NORMAL_WIDTH = 100,
};

// usWidthClass 1 to 9 in percent, as OpenType gives them.
static const float s_widths[9] = {50.0f,  62.5f,  75.0f,  87.5f, 100.0f,
                                  112.5f, 125.0f, 150.0f, 200.0f};

typedef struct Range
{
    float low;
    float high;
} Range;

typedef struct Face
{
    const muiFont* font;
    muiFontId id;
} Face;

static uint32_t Tag(char a, char b, char c, char d)
{
    return (uint32_t)(unsigned char)a << 24 | (uint32_t)(unsigned char)b << 16 |
           (uint32_t)(unsigned char)c << 8 | (uint32_t)(unsigned char)d;
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

// The values an axis reaches, or value alone without it.
static Range RangeOf(const muiFont* font, uint32_t tag, float value)
{
    const muiFontAxis* axis = FindAxis(font, tag);
    return axis != nullptr
               ? (Range){(float)axis->minimum / 65536.0f, (float)axis->maximum / 65536.0f}
               : (Range){value, value};
}

static bool CanSlant(const muiFont* font, muiFontSlant slant)
{
    Range ital = RangeOf(font, Tag('i', 't', 'a', 'l'), -1.0f);
    Range slnt = RangeOf(font, Tag('s', 'l', 'n', 't'), 1.0f);
    switch (slant)
    {
    case mui_slantItalic:
        return font->faceSlant == mui_slantItalic || ital.high >= 1.0f;
    case mui_slantOblique:
        return font->faceSlant == mui_slantOblique || slnt.low < 0.0f;
    default:
        return font->faceSlant == mui_slantNormal || ital.low == 0.0f ||
               (slnt.low <= 0.0f && slnt.high >= 0.0f);
    }
}

// Keeps the faces whose width range holds normal; else the narrower ones
// nearest it; else the wider ones nearest it. Returns how many are kept.
static uint32_t MatchWidth(Face* faces, uint32_t count)
{
    float best = 0.0f;
    int tier = 3;
    for (uint32_t i = 0; i < count; i++)
    {
        Range width = RangeOf(faces[i].font, Tag('w', 'd', 't', 'h'),
                              s_widths[faces[i].font->widthClass - 1]);
        int own = width.low <= NORMAL_WIDTH && width.high >= NORMAL_WIDTH ? 0
                  : width.high < NORMAL_WIDTH                             ? 1
                                                                          : 2;
        float distance = own == 1 ? NORMAL_WIDTH - width.high : width.low - NORMAL_WIDTH;
        if (own < tier || (own == tier && own != 0 && distance < best))
        {
            tier = own;
            best = distance;
        }
    }
    uint32_t kept = 0;
    for (uint32_t i = 0; i < count; i++)
    {
        Range width = RangeOf(faces[i].font, Tag('w', 'd', 't', 'h'),
                              s_widths[faces[i].font->widthClass - 1]);
        bool keep = tier == 0   ? width.low <= NORMAL_WIDTH && width.high >= NORMAL_WIDTH
                    : tier == 1 ? width.high < NORMAL_WIDTH && NORMAL_WIDTH - width.high == best
                                : width.low > NORMAL_WIDTH && width.low - NORMAL_WIDTH == best;
        if (keep)
        {
            faces[kept++] = faces[i];
        }
    }
    return kept;
}

// Keeps the faces of the first style, in CSS's order for the slant asked
// for, that any face can take.
static uint32_t MatchSlant(Face* faces, uint32_t count, muiFontSlant slant)
{
    static const muiFontSlant orders[3][3] = {
        {mui_slantNormal, mui_slantOblique, mui_slantItalic},
        {mui_slantItalic, mui_slantOblique, mui_slantNormal},
        {mui_slantOblique, mui_slantItalic, mui_slantNormal},
    };
    for (int k = 0; k < 3; k++)
    {
        muiFontSlant style = orders[slant][k];
        uint32_t kept = 0;
        for (uint32_t i = 0; i < count; i++)
        {
            if (CanSlant(faces[i].font, style))
            {
                faces[kept++] = faces[i];
            }
        }
        if (kept != 0)
        {
            return kept;
        }
    }
    return count;
}

// How well a face's weights suit a weight, as CSS orders them: lower is
// better; a face that reaches it is best.
static float WeightScore(const muiFont* font, float weight)
{
    Range range = RangeOf(font, Tag('w', 'g', 'h', 't'), (float)font->weightClass);
    if (range.low <= weight && range.high >= weight)
    {
        return -1.0f;
    }
    bool above = range.low > weight;
    float distance = above ? range.low - weight : weight - range.high;
    if (weight >= 400.0f && weight <= 500.0f)
    {
        // Up to 500 ascending, then below descending, then above 500.
        int tier = above && range.low <= 500.0f ? 0 : (!above ? 1 : 2);
        return (float)tier * 2000.0f + distance;
    }
    // Below 400 lighter faces first, above 500 heavier ones.
    bool first = weight < 400.0f ? !above : above;
    return (first ? 0.0f : 2000.0f) + distance;
}

const muiFont* muiMatchFamily(const muiTextService* service, const muiFontFamily* family,
                              float weight, muiFontSlant slant, muiFontId* faceOut)
{
    Face faces[MAX_FACES];
    uint32_t count = 0;
    for (uint32_t i = 0; i < family->faceCount; i++)
    {
        muiFontId id = family->faces[i];
        uint32_t slot = muiPoolResolve(&service->fonts.pool, id.index1, id.generation);
        if (slot != 0)
        {
            faces[count++] = (Face){&service->fonts.fonts[slot - 1], id};
        }
    }
    if (count == 0)
    {
        return nullptr;
    }
    count = MatchWidth(faces, count);
    count = MatchSlant(faces, count, slant <= mui_slantOblique ? slant : mui_slantNormal);
    uint32_t best = 0;
    float bestScore = WeightScore(faces[0].font, weight);
    for (uint32_t i = 1; i < count; i++)
    {
        float score = WeightScore(faces[i].font, weight);
        if (score < bestScore)
        {
            best = i;
            bestScore = score;
        }
    }
    *faceOut = faces[best].id;
    return faces[best].font;
}

const muiFontFamily* muiFindFamily(const muiTextService* service, uint64_t key)
{
    if ((key & MUI_FAMILY_BIT) == 0 || (key & ~(MUI_FAMILY_BIT | MUI_FONT_PART_MASK)) != 0)
    {
        return nullptr;
    }
    uint32_t slot = (uint32_t)(key & 0xFFFFu) + 1;
    uint32_t generation = (uint32_t)(key >> 16) & 0xFFFFFFu;
    const muiPool* pool = &service->families.pool;
    bool live = slot <= pool->used && pool->slots[slot - 1].live &&
                muiKeyGeneration(pool->slots[slot - 1].generation) == generation;
    return live ? &service->families.families[slot - 1] : nullptr;
}

// Whether a key names a live font or family: invalid for 0 or a key with
// an instance's bits, stale for one that names nothing now.
static muiResult CheckKey(const muiTextService* service, uint64_t key)
{
    if ((key & MUI_FONT_PART_MASK) == 0 || (key & ~(MUI_FAMILY_BIT | MUI_FONT_PART_MASK)) != 0)
    {
        return mui_errorInvalid;
    }
    uint64_t found = 0;
    bool live = (key & MUI_FAMILY_BIT) != 0 ? muiFindFamily(service, key) != nullptr
                                            : muiFindFont(service, key, &found) != nullptr;
    return live ? mui_success : mui_errorStale;
}

// Checks keys as fallbacks: up to MUI_MAX_FALLBACKS, each live.
static muiResult CheckFallbacks(const muiTextService* service, const uint64_t* keys, uint32_t count)
{
    if ((keys == nullptr && count != 0) || count > MUI_MAX_FALLBACKS)
    {
        return mui_errorInvalid;
    }
    muiResult result = mui_success;
    for (uint32_t i = 0; i < count && result == mui_success; i++)
    {
        result = CheckKey(service, keys[i]);
    }
    return result;
}

muiResult muiSetFallbackFonts(muiTextService* service, const uint64_t* keys, uint32_t count)
{
    if (service == nullptr)
    {
        return mui_errorInvalid;
    }
    muiResult result = CheckFallbacks(service, keys, count);
    if (result == mui_success)
    {
        for (uint32_t i = 0; i < count; i++)
        {
            service->fallbacks[i] = keys[i];
        }
        service->fallbackCount = count;
    }
    return result;
}

muiFontFamilyDef muiDefaultFontFamilyDef(void)
{
    return (muiFontFamilyDef){.cookie = FONT_FAMILY_DEF_COOKIE};
}

static uint32_t ResolveFamily(const muiTextService* service, muiFontFamilyId familyId)
{
    return muiPoolResolve(&service->families.pool, familyId.index1, familyId.generation);
}

muiResult muiCreateFontFamily(muiTextService* service, const muiFontFamilyDef* def,
                              muiFontFamilyId* familyOut)
{
    if (familyOut != nullptr)
    {
        *familyOut = (muiFontFamilyId){0};
    }
    if (service == nullptr || def == nullptr || familyOut == nullptr ||
        def->cookie != FONT_FAMILY_DEF_COOKIE || def->faces == nullptr || def->faceCount == 0 ||
        def->faceCount > MAX_FACES)
    {
        return mui_errorInvalid;
    }
    for (uint32_t i = 0; i < def->faceCount; i++)
    {
        if (!muiFont_IsValid(service, def->faces[i]))
        {
            return mui_errorStale;
        }
    }
    muiResult checked = CheckFallbacks(service, def->fallbacks, def->fallbackCount);
    if (checked != mui_success)
    {
        return checked;
    }
    muiFamilyStore* store = &service->families;
    uint32_t slot = muiPoolTake(&store->pool);
    if (slot == 0)
    {
        return mui_errorCapacity;
    }
    size_t bytes = (size_t)def->faceCount * sizeof(muiFontId);
    muiFontId* faces = muiAllocate(&service->allocator, bytes, alignof(muiFontId));
    if (faces == nullptr)
    {
        muiPoolGive(&store->pool, slot);
        return mui_errorCapacity;
    }
    memcpy(faces, def->faces, bytes);
    muiFontFamily* family = &store->families[slot - 1];
    *family = (muiFontFamily){.faces = faces, .faceCount = def->faceCount};
    for (uint32_t i = 0; i < def->fallbackCount; i++)
    {
        family->fallbacks[i] = def->fallbacks[i];
    }
    family->fallbackCount = def->fallbackCount;
    *familyOut = (muiFontFamilyId){slot, muiPoolGeneration(&store->pool, slot)};
    return mui_success;
}

muiResult muiDestroyFontFamily(muiTextService* service, muiFontFamilyId familyId)
{
    if (service == nullptr || familyId.index1 == 0)
    {
        return mui_errorInvalid;
    }
    uint32_t slot = ResolveFamily(service, familyId);
    if (slot == 0)
    {
        return mui_errorStale;
    }
    muiReleaseFontFamily(&service->allocator, &service->families.families[slot - 1]);
    muiPoolGive(&service->families.pool, slot);
    return mui_success;
}

bool muiFontFamily_IsValid(const muiTextService* service, muiFontFamilyId familyId)
{
    return service != nullptr && familyId.index1 != 0 && ResolveFamily(service, familyId) != 0;
}

muiResult muiFontFamily_MatchFace(const muiTextService* service, muiFontFamilyId familyId,
                                  float weight, uint8_t slant, muiFontId* faceOut)
{
    if (service == nullptr || faceOut == nullptr || familyId.index1 == 0 ||
        !(weight >= 1.0f && weight <= 1000.0f) || slant > mui_slantOblique)
    {
        return mui_errorInvalid;
    }
    uint32_t slot = ResolveFamily(service, familyId);
    if (slot == 0)
    {
        return mui_errorStale;
    }
    muiFontId face = {0, 0};
    if (muiMatchFamily(service, &service->families.families[slot - 1], weight, slant, &face) ==
        nullptr)
    {
        return mui_errorStale;
    }
    *faceOut = face;
    return mui_success;
}
