// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Little-endian bytes for the tile formats (mnav-0003, mnav-0015): a
// writer into a buffer sized beforehand and a reader that treats its
// bytes as hostile, reading 0 past their end.

#ifndef MAUL_NAV_SRC_BYTES_H
#define MAUL_NAV_SRC_BYTES_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>

// Bytes written in order, little-endian.
typedef struct mnavByteWriter
{
    uint8_t* at;
} mnavByteWriter;

static inline void mnavPutU8(mnavByteWriter* w, uint64_t v)
{
    *w->at++ = (uint8_t)v;
}

static inline void mnavPutU16(mnavByteWriter* w, uint64_t v)
{
    mnavPutU8(w, v);
    mnavPutU8(w, v >> 8);
}

static inline void mnavPutU32(mnavByteWriter* w, uint64_t v)
{
    mnavPutU16(w, v);
    mnavPutU16(w, v >> 16);
}

static inline void mnavPutU64(mnavByteWriter* w, uint64_t v)
{
    mnavPutU32(w, v);
    mnavPutU32(w, v >> 32);
}

// Bytes read in order, little-endian; reading past the end sets exhausted
// and reads 0.
typedef struct mnavByteReader
{
    const uint8_t* at;
    size_t left;
    bool exhausted;
} mnavByteReader;

static inline uint64_t mnavGetU8(mnavByteReader* r)
{
    if (r->left < 1)
    {
        r->exhausted = true;
        return 0;
    }
    r->left -= 1;
    return *r->at++;
}

static inline uint64_t mnavGetU16(mnavByteReader* r)
{
    uint64_t low = mnavGetU8(r);
    return low | mnavGetU8(r) << 8;
}

static inline uint64_t mnavGetU32(mnavByteReader* r)
{
    uint64_t low = mnavGetU16(r);
    return low | mnavGetU16(r) << 16;
}

static inline uint64_t mnavGetU64(mnavByteReader* r)
{
    uint64_t low = mnavGetU32(r);
    return low | mnavGetU32(r) << 32;
}

static inline uint32_t mnavFloatBits(float f)
{
    uint32_t bits = 0;
    memcpy(&bits, &f, sizeof(bits));
    return bits;
}

static inline uint64_t mnavDoubleBits(double d)
{
    uint64_t bits = 0;
    memcpy(&bits, &d, sizeof(bits));
    return bits;
}

static inline float mnavBitsFloat(uint32_t bits)
{
    float f = 0.0f;
    memcpy(&f, &bits, sizeof(f));
    return f;
}

static inline double mnavBitsDouble(uint64_t bits)
{
    double d = 0.0;
    memcpy(&d, &bits, sizeof(d));
    return d;
}

#endif // MAUL_NAV_SRC_BYTES_H
