// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The fingerprint of a tile's input (mnav-0003): a 64-bit hash of every
// input triangle that reaches the tile as rasterization picks it, and of
// the volumes that reach it.

#ifndef MAUL_NAV_SRC_FINGERPRINT_H
#define MAUL_NAV_SRC_FINGERPRINT_H

#include "raster.h"

#include "maul-nav/bake.h"

#include <stdint.h>

// Adds count 32-bit words to a hash.
uint64_t mnavHashWords(uint64_t hash, const uint32_t* words, int32_t count);

// Adds every input triangle that reaches the frame, meshes' (only those
// the input's tile index lists for tile (tileX, tileZ), when it has one)
// then terrains', with the area a slope of cosMaxSlope gives it, and then
// the volumes, to a hash; count receives the triangles.
uint64_t mnavFingerprintInput(const mnavTileFrame* frame, float cosMaxSlope,
                              const mnavBakeInput* input, int32_t tileX, int32_t tileZ,
                              uint64_t hash, int32_t* count);

#endif // MAUL_NAV_SRC_FINGERPRINT_H
