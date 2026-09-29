// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// A window icon as its request holds it, for the backends.

#ifndef MAUL_WINDOW_SRC_ICON_H
#define MAUL_WINDOW_SRC_ICON_H

#include "core.h"

// An image of the copy: its pixels packed, width * 4 bytes a row.
typedef struct mwinIconCopyImage
{
    uint32_t width;
    uint32_t height;
    const uint8_t* pixels;
} mwinIconCopyImage;

// The images, in one block from the allocator of size bytes, their
// pixels after them.
typedef struct mwinIconCopy
{
    size_t size;
    uint32_t count;
    mwinIconCopyImage images[];
} mwinIconCopy;

// The image a platform takes for a size: the smallest at least as
// large, else the largest.
const mwinIconCopyImage* mwinIconFor(const mwinIconCopy* icon, uint32_t size);

#endif // MAUL_WINDOW_SRC_ICON_H
