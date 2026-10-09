// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The flight tile format (mnav-0015): a tile's octrees as little-endian
// integers behind a versioned, fingerprinted header, and the loader that
// checks every byte of it as hostile.

#ifndef MAUL_NAV_SRC_FLIGHT_TILE_H
#define MAUL_NAV_SRC_FLIGHT_TILE_H

#include "allocator.h"
#include "flight.h"
#include "flight_def.h"

#include "maul-nav/base.h"
#include "maul-nav/flight.h"
#include "maul-nav/navmesh.h"

#include <stddef.h>
#include <stdint.h>

// The header's size in bytes.
#define MNAV_FLIGHT_HEADER_BYTES 104

// Writes a tile baked with def, its generator's version and fingerprint,
// into bytes allocated from memory, which mnavReleaseFlightTileBytes
// frees. The payload is a class byte per cube, a mask per node and a word
// per leaf; child and root indices are not stored.
mnavResult mnavEncodeFlightTile(mnavMemory* memory, const mnavFlightDef* def, mnavVersion generator,
                                uint64_t fingerprint, const mnavFlightTile* tile, uint8_t** bytes,
                                size_t* size);

void mnavReleaseFlightTileBytes(mnavMemory* memory, uint8_t* bytes, size_t size);

// Reads a tile from size bytes, checking it against a valid def and its
// shape: on success tile holds it, with its indices rebuilt, and
// fingerprintOut its fingerprint; on failure nothing is held. A
// malformed tile or one baked with other settings is mnav_errorInvalid,
// another format version mnav_errorVersion, counts past the def's limits
// mnav_errorLimit.
mnavTileResult mnavDecodeFlightTile(mnavMemory* memory, const mnavFlightDef* def,
                                    const mnavFlightShape* shape, const uint8_t* bytes, size_t size,
                                    mnavFlightTile* tile, uint64_t* fingerprintOut);

#endif // MAUL_NAV_SRC_FLIGHT_TILE_H
