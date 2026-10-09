// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Creating and destroying text services (record mui-0006).

#include "text_service.h"

#include "allocator.h"
#include "font_instance.h"
#include "pool.h"

#include "maul-unicode/harfbuzz.h"

#include <limits.h>
#include <stdalign.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define TEXT_SERVICE_DEF_COOKIE 0x6D757478u // "mutx"

enum
{
    MAX_FONTS = 65536,
    MAX_FAMILIES = 65536,
    MAX_BLOCKS = 1 << 24,
};

// FreeType's memory through the service's allocator. FreeType does not
// give a block's size when it frees it, so each block starts with a
// header holding the size it was allocated with; the header keeps the
// alignment of max_align_t, which FreeType expects of malloc.
enum
{
    HEADER = alignof(max_align_t)
};

// A size FreeType gives, a long, always fits with the header.
static_assert((unsigned long long)LONG_MAX <= SIZE_MAX - HEADER, "a block's size fits size_t");

static void* FreeTypeAlloc(FT_Memory memory, long size)
{
    const muiTextService* service = memory->user;
    if (size <= 0)
    {
        return nullptr;
    }
    size_t total = (size_t)size + HEADER;
    unsigned char* block = muiAllocate(&service->allocator, total, HEADER);
    if (block == nullptr)
    {
        return nullptr;
    }
    memcpy(block, &total, sizeof total);
    return block + HEADER;
}

static void FreeTypeFree(FT_Memory memory, void* block)
{
    if (block == nullptr)
    {
        return;
    }
    const muiTextService* service = memory->user;
    unsigned char* start = (unsigned char*)block - HEADER;
    size_t total = 0;
    memcpy(&total, start, sizeof total);
    muiRelease(&service->allocator, start, total, HEADER);
}

// A new block with the old one's first bytes: the allocator has no
// reallocation.
static void* FreeTypeRealloc(FT_Memory memory, long currentSize, long newSize, void* block)
{
    void* moved = FreeTypeAlloc(memory, newSize);
    if (moved == nullptr)
    {
        return nullptr;
    }
    if (block != nullptr)
    {
        long kept = currentSize < newSize ? currentSize : newSize;
        memcpy(moved, block, kept > 0 ? (size_t)kept : 0u);
        FreeTypeFree(memory, block);
    }
    return moved;
}

muiTextServiceDef muiDefaultTextServiceDef(void)
{
    return (muiTextServiceDef){
        .cookie = TEXT_SERVICE_DEF_COOKIE,
        .limits = {.fonts = 64, .textBlocks = 1024, .fontFamilies = 16},
    };
}

typedef struct Parts
{
    size_t fontSlots;
    size_t fonts;
    size_t blockSlots;
    size_t blocks;
    size_t familySlots;
    size_t families;
} Parts;

static Parts LayOut(muiLayout* layout, const muiTextLimits* limits)
{
    (void)muiLayoutAdd(layout, 1, sizeof(muiTextService), alignof(muiTextService));
    Parts parts = {0};
    parts.fontSlots =
        muiLayoutAdd(layout, limits->fonts, sizeof(muiPoolSlot), alignof(muiPoolSlot));
    parts.fonts = muiLayoutAdd(layout, limits->fonts, sizeof(muiFont), alignof(muiFont));
    parts.blockSlots =
        muiLayoutAdd(layout, limits->textBlocks, sizeof(muiPoolSlot), alignof(muiPoolSlot));
    parts.blocks =
        muiLayoutAdd(layout, limits->textBlocks, sizeof(muiTextBlock), alignof(muiTextBlock));
    parts.familySlots =
        muiLayoutAdd(layout, limits->fontFamilies, sizeof(muiPoolSlot), alignof(muiPoolSlot));
    parts.families =
        muiLayoutAdd(layout, limits->fontFamilies, sizeof(muiFontFamily), alignof(muiFontFamily));
    return parts;
}

// Whether every module the text component reads fonts with is there.
static bool HasModules(FT_Library library)
{
    static const char* const names[] = {"truetype", "cff", "sfnt", "psaux", "psnames", "smooth"};
    for (size_t i = 0; i < sizeof names / sizeof names[0]; i++)
    {
        if (FT_Get_Module(library, names[i]) == nullptr)
        {
            return false;
        }
    }
    return true;
}

static void Release(muiTextService* service)
{
    muiFreeBuffer(&service->allocator, &service->lines);
    muiFreeBuffer(&service->allocator, &service->runs);
    muiFreeBuffer(&service->allocator, &service->glyphs);
    muiFreeBuffer(&service->allocator, &service->workspace);
    muiFreeBuffer(&service->allocator, &service->lineItems);
    muiFreeBuffer(&service->allocator, &service->lineGlyphs);
    muiFreeBuffer(&service->allocator, &service->hitBoxes);
    muiFreeBuffer(&service->allocator, &service->editScratch);
    muiFreeBuffer(&service->allocator, &service->fieldSegments);
    muiFreeBuffer(&service->allocator, &service->fieldPieces);
    muiFreeBuffer(&service->allocator, &service->fieldOrigins);
    muiFreeBuffer(&service->allocator, &service->fieldRows);
    muiFreeBuffer(&service->allocator, &service->fieldCrossings);
    muiFreeBuffer(&service->allocator, &service->fieldCells);
    muiFreeBuffer(&service->allocator, &service->fieldCurves);
    muiFreeBuffer(&service->allocator, &service->fieldEdgeOrigins);
    muiFreeBuffer(&service->allocator, &service->fieldEdgeSides);
    muiFreeBuffer(&service->allocator, &service->fieldEdgeColors);
    muiFreeBuffer(&service->allocator, &service->fieldLoops);
    muiFreeBuffer(&service->allocator, &service->fieldChannels);
    muiFreeBuffer(&service->allocator, &service->fieldInside);
    muiFreeBuffer(&service->allocator, &service->colorCoverage);
    muiFreeBuffer(&service->allocator, &service->colorPixels);
    muiFreeBuffer(&service->allocator, &service->paintSurfaces);
    muiFreeBuffer(&service->allocator, &service->paintStops);
    muiFreeBuffer(&service->allocator, &service->bitmapScratch);
    muiFreeBuffer(&service->allocator, &service->bitmapRgba);
    muiFreeBuffer(&service->allocator, &service->bitmapLinear);
    muiFreeBuffer(&service->allocator, &service->fieldCellPieces);
    muiFreeBuffer(&service->allocator, &service->fieldEdge);
    muiFreeBuffer(&service->allocator, &service->fieldDistances);
    if (service->unicode != nullptr)
    {
        hb_unicode_funcs_destroy(service->unicode);
    }
    if (service->freetype != nullptr)
    {
        (void)FT_Done_Library(service->freetype);
    }
    muiAllocator allocator = service->allocator;
    muiRelease(&allocator, service, service->blockSize, alignof(max_align_t));
}

muiResult muiCreateTextService(const muiTextServiceDef* def, muiTextService** serviceOut)
{
    if (serviceOut != nullptr)
    {
        *serviceOut = nullptr;
    }
    if (def == nullptr || serviceOut == nullptr || def->cookie != TEXT_SERVICE_DEF_COOKIE ||
        !muiIsAllocatorValid(&def->allocator) || def->limits.fonts == 0 ||
        def->limits.fonts > MAX_FONTS || def->limits.textBlocks > MAX_BLOCKS ||
        def->limits.fontFamilies > MAX_FAMILIES)
    {
        return mui_errorInvalid;
    }
    muiLayout layout = {0};
    Parts parts = LayOut(&layout, &def->limits);
    if (layout.overflow)
    {
        return mui_errorCapacity;
    }
    unsigned char* block = muiAllocate(&def->allocator, layout.size, alignof(max_align_t));
    if (block == nullptr)
    {
        return mui_errorCapacity;
    }
    memset(block, 0, layout.size);
    muiTextService* service = (muiTextService*)block;
    service->allocator = def->allocator;
    service->blockSize = layout.size;
    service->limits = def->limits;
    muiPoolInit(&service->fonts.pool, (muiPoolSlot*)(block + parts.fontSlots), def->limits.fonts);
    service->fonts.fonts = (muiFont*)(block + parts.fonts);
    muiPoolInit(&service->blocks.pool, (muiPoolSlot*)(block + parts.blockSlots),
                def->limits.textBlocks);
    service->blocks.blocks = (muiTextBlock*)(block + parts.blocks);
    muiPoolInit(&service->families.pool, (muiPoolSlot*)(block + parts.familySlots),
                def->limits.fontFamilies);
    service->families.families = (muiFontFamily*)(block + parts.families);
    service->memory = (struct FT_MemoryRec_){
        .user = service,
        .alloc = FreeTypeAlloc,
        .free = FreeTypeFree,
        .realloc = FreeTypeRealloc,
    };
    if (FT_New_Library(&service->memory, &service->freetype) != 0)
    {
        service->freetype = nullptr;
        Release(service);
        return mui_errorCapacity;
    }
    // Adding a module can run out of memory, which FreeType does not
    // report here; a module missing would make every font unreadable.
    FT_Add_Default_Modules(service->freetype);
    if (!HasModules(service->freetype))
    {
        Release(service);
        return mui_errorCapacity;
    }
    service->unicode = muniCreateHarfBuzzFunctions();
    if (service->unicode == hb_unicode_funcs_get_empty())
    {
        service->unicode = nullptr;
        Release(service);
        return mui_errorCapacity;
    }
    *serviceOut = service;
    return mui_success;
}

uint64_t muiGetTextServiceMisuse(const muiTextService* service)
{
    return service != nullptr ? service->misuse : 0;
}

void muiDestroyTextService(muiTextService* service)
{
    if (service == nullptr)
    {
        return;
    }
    for (uint32_t slot = 1; slot <= service->fonts.pool.used; slot++)
    {
        if (service->fonts.pool.slots[slot - 1].live)
        {
            muiReleaseFont(&service->allocator, &service->fonts.fonts[slot - 1]);
        }
    }
    for (uint32_t slot = 1; slot <= service->blocks.pool.used; slot++)
    {
        if (service->blocks.pool.slots[slot - 1].live)
        {
            muiReleaseTextBlock(&service->allocator, &service->blocks.blocks[slot - 1]);
        }
    }
    for (uint32_t slot = 1; slot <= service->families.pool.used; slot++)
    {
        if (service->families.pool.slots[slot - 1].live)
        {
            muiReleaseFontFamily(&service->allocator, &service->families.families[slot - 1]);
        }
    }
    Release(service);
}

muiFont* muiFindFont(const muiTextService* service, uint64_t key, uint64_t* keyOut)
{
    if (key == 0 && service->defaultFont.index1 != 0)
    {
        key = muiKeyOf(service->defaultFont.index1, service->defaultFont.generation);
    }
    *keyOut = key;
    uint32_t slot = (uint32_t)(key & 0xFFFFu) + 1;
    uint32_t generation = (uint32_t)(key >> 16) & 0xFFFFFFu;
    const muiPool* pool = &service->fonts.pool;
    // Bit 63 is kept for families' keys.
    bool live = (key & MUI_FONT_PART_MASK) != 0 && key >> 63 == 0 && slot <= pool->used &&
                pool->slots[slot - 1].live &&
                muiKeyGeneration(pool->slots[slot - 1].generation) == generation;
    return live ? &service->fonts.fonts[slot - 1] : nullptr;
}
