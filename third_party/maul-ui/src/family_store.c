// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Font family records.

#include "family_store.h"

#include "allocator.h"

#include <stdalign.h>

void muiDropFontFamily(const muiAllocator* allocator, muiFamilyStore* store, uint32_t slot)
{
    muiFontFamily family = store->families[slot - 1];
    store->families[slot - 1] = (muiFontFamily){0};
    muiPoolGive(&store->pool, slot);
    muiReleaseFontFamily(allocator, &family);
}

void muiReleaseFontFamily(const muiAllocator* allocator, muiFontFamily* family)
{
    if (family->faces != nullptr)
    {
        muiRelease(allocator, family->faces, (size_t)family->faceCount * sizeof(muiFontId),
                   alignof(muiFontId));
    }
    *family = (muiFontFamily){0};
}
