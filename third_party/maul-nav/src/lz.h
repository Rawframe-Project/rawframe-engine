// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// A small LZ coder for cached tiles (mnav-0016), in LZ4's block format:
// each sequence a token byte (the literals' count in its high four bits,
// the match's length less four in its low four, either at 15 continued
// by bytes added on until one under 255), the literals, then a two-byte
// little-endian offset back into the last 65,535 bytes and the match's
// further length bytes. The last sequence has literals only.

#ifndef MAUL_NAV_SRC_LZ_H
#define MAUL_NAV_SRC_LZ_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Entries in the encoder's table of places: 2^MNAV_LZ_HASH_BITS.
#define MNAV_LZ_HASH_BITS 12
#define MNAV_LZ_TABLE     (1u << MNAV_LZ_HASH_BITS)

// The most bytes size bytes may take coded.
size_t mnavLzBound(size_t size);

// Codes size bytes of in into out, which holds mnavLzBound(size) bytes;
// table holds MNAV_LZ_TABLE entries, its contents unused. Returns the
// bytes written.
size_t mnavLzEncode(const uint8_t* in, size_t size, uint8_t* out, uint32_t* table);

// Decodes size bytes of in into exactly outSize bytes of out. False for
// bytes that are not such a coding, nothing read or written out of range.
bool mnavLzDecode(const uint8_t* in, size_t size, uint8_t* out, size_t outSize);

#endif // MAUL_NAV_SRC_LZ_H
