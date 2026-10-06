// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Font family records (record mui-0006).

#include "family_store.h"

#include "allocator.h"

#include <stdalign.h>

void muiReleaseFontFamily(const muiAllocator* allocator, muiFontFamily* family)
{
    if (family->faces != nullptr)
    {
        muiRelease(allocator, family->faces, (size_t)family->faceCount * sizeof(muiFontId),
                   alignof(muiFontId));
    }
    *family = (muiFontFamily){0};
}
