// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Triangulating a simple ring by ear clipping.

#ifndef MAUL_NAV_SRC_TRIANGULATE_H
#define MAUL_NAV_SRC_TRIANGULATE_H

#include "contour.h"

#include <stdint.h>

// The scratch a ring of count vertices needs: an index and an ear flag
// per vertex.
typedef struct mnavEarScratch
{
    int32_t* indices;
    uint8_t* ears;
} mnavEarScratch;

// Cuts a ring of count vertices into triangles, the ear with the shortest
// diagonal first, and writes each as three ring indices to triangles,
// which holds count - 2. Returns the number written; complete is false
// when no ear was left before the end, in which case the triangles cut so
// far are written.
int32_t mnavTriangulate(const mnavContourVertex* ring, int32_t count, mnavEarScratch scratch,
                        int32_t* triangles, bool* complete);

#endif // MAUL_NAV_SRC_TRIANGULATE_H
