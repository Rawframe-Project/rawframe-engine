// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Fonts (record mui-0006). FreeType opens and validates a face; HarfBuzz
// reads the same bytes for shaping. Metrics are read once, from the
// OpenType tables, in integers divided by the units per em.

#include "maul-ui/font.h"

#include "allocator.h"
#include "font_store.h"
#include "pool.h"
#include "text_service.h"

#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_TRUETYPE_TABLES_H
#include FT_MULTIPLE_MASTERS_H
#include FT_TRUETYPE_TAGS_H
#include FT_PARAMETER_TAGS_H

#include <stdint.h>
#include <string.h>

#define FONT_DEF_COOKIE 0x6D75666Eu // "mufn"

enum
{
    // The smallest file with an sfnt or collection header.
    HEADER_SIZE = 12,
    // OS/2 fsSelection: the face is italic; it is oblique; the
    // typographic metrics are the ones to use.
    ITALIC = 1u << 0,
    OBLIQUE = 1u << 9,
    USE_TYPO_METRICS = 1u << 7,
};

muiFontDef muiDefaultFontDef(void)
{
    return (muiFontDef){.cookie = FONT_DEF_COOKIE};
}

static uint32_t ReadTag(const unsigned char* bytes)
{
    return (uint32_t)bytes[0] << 24 | (uint32_t)bytes[1] << 16 | (uint32_t)bytes[2] << 8 |
           (uint32_t)bytes[3];
}

static uint32_t Tag(char a, char b, char c, char d)
{
    return (uint32_t)(unsigned char)a << 24 | (uint32_t)(unsigned char)b << 16 |
           (uint32_t)(unsigned char)c << 8 | (uint32_t)(unsigned char)d;
}

muiResult muiCountFontFaces(const void* data, size_t size, uint32_t* countOut)
{
    if (countOut != nullptr)
    {
        *countOut = 0;
    }
    if (data == nullptr || countOut == nullptr)
    {
        return mui_errorInvalid;
    }
    if (size < HEADER_SIZE)
    {
        return mui_errorFormat;
    }
    const unsigned char* bytes = data;
    uint32_t tag = ReadTag(bytes);
    // TrueType outlines, CFF outlines, and Apple's TrueType tag.
    if (tag == 0x00010000u || tag == Tag('O', 'T', 'T', 'O') || tag == Tag('t', 'r', 'u', 'e'))
    {
        *countOut = 1;
        return mui_success;
    }
    if (tag != Tag('t', 't', 'c', 'f'))
    {
        return mui_errorFormat;
    }
    // A collection: its version, its count and an offset per face.
    uint32_t count = ReadTag(bytes + 8);
    if (count == 0 || count > (size - HEADER_SIZE) / 4)
    {
        return mui_errorFormat;
    }
    *countOut = count;
    return mui_success;
}

// The units per em, from the head table: FreeType gives 0 for a face
// without outlines, as a bitmap-only emoji font is.
static uint32_t UnitsPerEm(FT_Face face)
{
    const TT_Header* head = FT_Get_Sfnt_Table(face, FT_SFNT_HEAD);
    return head != nullptr ? head->Units_Per_EM : face->units_per_EM;
}

// A table as a view of the font's bytes, which HarfBuzz's face reads in
// place; none when it is missing or, from a HarfBuzz that copied it,
// not within them.
static muiFontTable TableOf(const muiFont* font, hb_tag_t tag)
{
    hb_blob_t* blob = hb_face_reference_table(font->shapingFace, tag);
    unsigned int length = 0;
    const char* data = hb_blob_get_data(blob, &length);
    hb_blob_destroy(blob);
    const unsigned char* start = (const unsigned char*)data;
    bool within = data != nullptr && length > 0 && start >= font->data &&
                  (size_t)(start - font->data) <= font->size &&
                  length <= font->size - (size_t)(start - font->data);
    return within ? (muiFontTable){start, length} : (muiFontTable){nullptr, 0};
}

static float Ems(int32_t units, uint32_t unitsPerEm)
{
    return (float)units / (float)unitsPerEm;
}

static void ReadMetrics(muiFont* font)
{
    FT_Face face = font->face;
    uint32_t unitsPerEm = UnitsPerEm(face);
    const TT_OS2* os2 = FT_Get_Sfnt_Table(face, FT_SFNT_OS2);
    const TT_HoriHeader* hhea = FT_Get_Sfnt_Table(face, FT_SFNT_HHEA);
    const TT_Postscript* post = FT_Get_Sfnt_Table(face, FT_SFNT_POST);
    bool hasOs2 = os2 != nullptr && os2->version != 0xFFFFu;
    bool hasTypo = hasOs2 && (os2->sTypoAscender != 0 || os2->sTypoDescender != 0);
    int32_t ascent = 0;
    int32_t descent = 0;
    int32_t lineGap = 0;
    if (hasOs2 && (os2->fsSelection & USE_TYPO_METRICS) != 0)
    {
        ascent = os2->sTypoAscender;
        descent = -os2->sTypoDescender;
        lineGap = os2->sTypoLineGap;
    }
    else if (hhea != nullptr && (hhea->Ascender != 0 || hhea->Descender != 0))
    {
        ascent = hhea->Ascender;
        descent = -hhea->Descender;
        lineGap = hhea->Line_Gap;
    }
    else if (hasTypo)
    {
        ascent = os2->sTypoAscender;
        descent = -os2->sTypoDescender;
        lineGap = os2->sTypoLineGap;
    }
    else if (hasOs2)
    {
        ascent = os2->usWinAscent;
        descent = os2->usWinDescent;
    }
    muiFontMetrics* metrics = &font->metrics;
    *metrics = (muiFontMetrics){
        .unitsPerEm = unitsPerEm,
        .glyphCount = (uint32_t)face->num_glyphs,
        .ascent = Ems(ascent, unitsPerEm),
        .descent = Ems(descent, unitsPerEm),
        .lineGap = Ems(lineGap, unitsPerEm),
    };
    if (hasOs2 && os2->version >= 2)
    {
        metrics->capHeight = Ems(os2->sCapHeight, unitsPerEm);
        metrics->xHeight = Ems(os2->sxHeight, unitsPerEm);
    }
    if (post != nullptr)
    {
        metrics->underlineOffset = Ems(-post->underlinePosition, unitsPerEm);
        metrics->underlineThickness = Ems(post->underlineThickness, unitsPerEm);
    }
    if (hasOs2)
    {
        metrics->strikeoutOffset = Ems(os2->yStrikeoutPosition, unitsPerEm);
        metrics->strikeoutThickness = Ems(os2->yStrikeoutSize, unitsPerEm);
    }
}

// Reads the face's variation axes, up to MUI_MAX_FONT_AXES, the weight,
// width and slant OS/2 gives it, and whether it has colour layers; false
// when memory runs out.
static bool ReadStyle(FT_Library library, muiFont* font)
{
    FT_Face face = font->face;
    const TT_OS2* os2 = FT_Get_Sfnt_Table(face, FT_SFNT_OS2);
    bool hasOs2 = os2 != nullptr && os2->version != 0xFFFFu;
    font->weightClass = hasOs2 && os2->usWeightClass != 0 ? os2->usWeightClass : 400;
    font->widthClass =
        hasOs2 && os2->usWidthClass >= 1 && os2->usWidthClass <= 9 ? os2->usWidthClass : 5;
    bool italic =
        hasOs2 ? (os2->fsSelection & ITALIC) != 0 : (face->style_flags & FT_STYLE_FLAG_ITALIC) != 0;
    bool oblique = hasOs2 && (os2->fsSelection & OBLIQUE) != 0;
    font->faceSlant = oblique ? mui_slantOblique : (italic ? mui_slantItalic : mui_slantNormal);
    FT_ULong colorLength = 0;
    font->colorLayers =
        FT_Load_Sfnt_Table(face, TTAG_COLR, 0, nullptr, &colorLength) == 0 && colorLength > 0;
    if (!FT_HAS_MULTIPLE_MASTERS(face))
    {
        return true;
    }
    FT_MM_Var* variation = nullptr;
    FT_Error error = FT_Get_MM_Var(face, &variation);
    if (error != 0)
    {
        // A variation table FreeType does not take leaves the font
        // without axes, as its default instance.
        return FT_ERROR_BASE(error) != FT_Err_Out_Of_Memory;
    }
    uint32_t count =
        variation->num_axis < MUI_MAX_FONT_AXES ? variation->num_axis : MUI_MAX_FONT_AXES;
    for (uint32_t i = 0; i < count; i++)
    {
        const FT_Var_Axis* axis = &variation->axis[i];
        font->axes[i] = (muiFontAxis){(uint32_t)axis->tag, axis->minimum, axis->def, axis->maximum};
    }
    font->axisCount = count;
    (void)FT_Done_MM_Var(library, variation);
    return true;
}

// Opens a face of def's data into font, which is zeroed; what it made
// stays in font for the caller to release on failure.
static muiResult Open(muiTextService* service, const muiFontDef* def, muiFont* font)
{
    font->size = def->size;
    font->data = def->data;
    if (def->dataMode == mui_fontDataCopy)
    {
        font->copy = muiAllocate(&service->allocator, def->size, 1);
        if (font->copy == nullptr)
        {
            return mui_errorCapacity;
        }
        memcpy(font->copy, def->data, def->size);
        font->data = font->copy;
    }
    // sbix is read here, not by FreeType, which would otherwise take a
    // font with sbix and outlines for one of bitmaps alone.
    FT_Parameter ignoreSbix = {FT_PARAM_TAG_IGNORE_SBIX, nullptr};
    FT_Open_Args args = {.flags = FT_OPEN_MEMORY | FT_OPEN_PARAMS,
                         .memory_base = font->data,
                         .memory_size = (FT_Long)font->size,
                         .num_params = 1,
                         .params = &ignoreSbix};
    FT_Error error = FT_Open_Face(service->freetype, &args, (FT_Long)def->faceIndex, &font->face);
    if (error != 0)
    {
        font->face = nullptr;
        return FT_ERROR_BASE(error) == FT_Err_Out_Of_Memory ? mui_errorCapacity : mui_errorFormat;
    }
    // FreeType accepts a font without glyphs. It refuses units per em out
    // of range too, but metrics divide by them, so an installed FreeType
    // is not trusted with that.
    if (UnitsPerEm(font->face) < 16 || UnitsPerEm(font->face) > 16384 ||
        font->face->num_glyphs <= 0)
    {
        return mui_errorFormat;
    }
    // HarfBuzz reads the bytes in place; its face keeps the blob.
    hb_blob_t* blob = hb_blob_create_or_fail((const char*)font->data, (unsigned int)font->size,
                                             HB_MEMORY_MODE_READONLY, nullptr, nullptr);
    if (blob == nullptr)
    {
        return mui_errorCapacity;
    }
    // A face HarfBuzz's checks do not find is damaged; past them, a face
    // not made is memory running out.
    if (def->faceIndex >= hb_face_count(blob))
    {
        hb_blob_destroy(blob);
        return mui_errorFormat;
    }
    font->shapingFace = hb_face_create_or_fail(blob, def->faceIndex);
    hb_blob_destroy(blob);
    if (font->shapingFace == nullptr)
    {
        return mui_errorCapacity;
    }
    // FreeType's count of glyphs is the font's: HarfBuzz's may differ in
    // a damaged font, and shaping keeps its glyphs below it.
    hb_face_set_glyph_count(font->shapingFace, (unsigned int)font->face->num_glyphs);
    hb_face_make_immutable(font->shapingFace);
    font->shapingFont = hb_font_create(font->shapingFace);
    if (font->shapingFont == hb_font_get_empty())
    {
        font->shapingFont = nullptr;
        return mui_errorCapacity;
    }
    int scale = (int)UnitsPerEm(font->face);
    hb_font_set_scale(font->shapingFont, scale, scale);
    hb_font_make_immutable(font->shapingFont);
    ReadMetrics(font);
    font->cblc = TableOf(font, HB_TAG('C', 'B', 'L', 'C'));
    font->cbdt = TableOf(font, HB_TAG('C', 'B', 'D', 'T'));
    font->sbix = TableOf(font, HB_TAG('s', 'b', 'i', 'x'));
    return ReadStyle(service->freetype, font) ? mui_success : mui_errorCapacity;
}

muiResult muiCreateFont(muiTextService* service, const muiFontDef* def, muiFontId* fontOut)
{
    if (fontOut != nullptr)
    {
        *fontOut = (muiFontId){0};
    }
    if (service == nullptr || def == nullptr || fontOut == nullptr ||
        def->cookie != FONT_DEF_COOKIE || def->data == nullptr || def->size < HEADER_SIZE ||
        def->size > INT32_MAX || def->dataMode > mui_fontDataBorrow)
    {
        return muiRefuseText(service);
    }
    uint32_t faces = 0;
    muiResult counted = muiCountFontFaces(def->data, def->size, &faces);
    if (counted != mui_success)
    {
        return counted;
    }
    if (def->faceIndex >= faces)
    {
        return mui_errorFormat;
    }
    muiFontStore* store = &service->fonts;
    uint32_t slot = muiPoolTake(&store->pool);
    if (slot == 0)
    {
        return mui_errorCapacity;
    }
    muiFont* font = &store->fonts[slot - 1];
    muiResult result = Open(service, def, font);
    if (result != mui_success)
    {
        muiReleaseFont(&service->allocator, font);
        muiPoolGive(&store->pool, slot);
        return result;
    }
    *fontOut = (muiFontId){slot, muiPoolGeneration(&store->pool, slot)};
    return mui_success;
}

static uint32_t Resolve(const muiTextService* service, muiFontId fontId)
{
    return muiPoolResolve(&service->fonts.pool, fontId.index1, fontId.generation);
}

muiResult muiDestroyFont(muiTextService* service, muiFontId fontId)
{
    if (service == nullptr || fontId.index1 == 0)
    {
        return muiRefuseText(service);
    }
    uint32_t slot = Resolve(service, fontId);
    if (slot == 0)
    {
        return mui_errorStale;
    }
    muiReleaseFont(&service->allocator, &service->fonts.fonts[slot - 1]);
    muiPoolGive(&service->fonts.pool, slot);
    return mui_success;
}

bool muiFont_IsValid(const muiTextService* service, muiFontId fontId)
{
    return service != nullptr && fontId.index1 != 0 && Resolve(service, fontId) != 0;
}

muiResult muiFont_GetMetrics(const muiTextService* service, muiFontId fontId,
                             muiFontMetrics* metricsOut)
{
    if (service == nullptr || metricsOut == nullptr || fontId.index1 == 0)
    {
        return mui_errorInvalid;
    }
    uint32_t slot = Resolve(service, fontId);
    if (slot == 0)
    {
        return mui_errorStale;
    }
    *metricsOut = service->fonts.fonts[slot - 1].metrics;
    return mui_success;
}
