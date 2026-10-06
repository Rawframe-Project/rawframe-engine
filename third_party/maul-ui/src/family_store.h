// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The font families of a text service (record mui-0006): each family's
// faces, a copy of the ids it was made with, in the service's memory.

#ifndef MAUL_UI_SRC_FAMILY_STORE_H
#define MAUL_UI_SRC_FAMILY_STORE_H

#include "pool.h"

#include "maul-ui/font.h"

#include <stdint.h>

enum
{
    // The most fallbacks a family, or the service, has.
    MUI_MAX_FALLBACKS = 8
};

typedef struct muiFontFamily
{
    muiFontId* faces;
    uint32_t faceCount;
    // Keys of fonts and families tried, in order, for characters its face
    // lacks.
    uint64_t fallbacks[MUI_MAX_FALLBACKS];
    uint32_t fallbackCount;
} muiFontFamily;

typedef struct muiFamilyStore
{
    muiPool pool;
    // Family i is families[i - 1].
    muiFontFamily* families;
} muiFamilyStore;

// Releases a family's faces and zeroes it.
void muiReleaseFontFamily(const muiAllocator* allocator, muiFontFamily* family);

#endif // MAUL_UI_SRC_FAMILY_STORE_H
