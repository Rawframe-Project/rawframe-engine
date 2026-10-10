// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Open-space fields packed for the tile cache (mnav-0016): each column's
// span count and each span's floor, height and area, in planes of bytes
// (low bytes, then high), LZ coded. Links are not kept: unpacking links
// the spans again as the bake does.

#ifndef MAUL_NAV_SRC_FIELD_PACK_H
#define MAUL_NAV_SRC_FIELD_PACK_H

#include "allocator.h"
#include "compact.h"
#include "raster.h"

#include "maul-nav/base.h"

#include <stddef.h>
#include <stdint.h>

typedef struct mnavPackedField
{
    mnavTileFrame frame;
    int32_t spanCount;
    uint8_t* bytes;
    size_t size;
} mnavPackedField;

// Packs a field into memory, working in scratch; on failure packed holds
// nothing.
mnavResult mnavPackField(mnavMemory* scratch, mnavMemory* memory, const mnavCompactField* field,
                         mnavPackedField* packed);

// Unpacks a field into memory and links its spans for an agent of height
// cells that steps step cells; on failure field holds nothing.
mnavResult mnavUnpackField(mnavMemory* memory, const mnavPackedField* packed, int32_t height,
                           int32_t step, mnavCompactField* field);

void mnavReleasePackedField(mnavMemory* memory, mnavPackedField* packed);

#endif // MAUL_NAV_SRC_FIELD_PACK_H
