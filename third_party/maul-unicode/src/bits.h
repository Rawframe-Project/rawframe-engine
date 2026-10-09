// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Bit counting on 64-bit words, through the compiler builtins GCC and
// Clang (clang-cl included) provide (family 0008 uses only what the
// compiler provides, which rules out <stdbit.h> from the C library), and
// the rank indexes built on it.

#ifndef MAUL_UNICODE_SRC_BITS_H
#define MAUL_UNICODE_SRC_BITS_H

#include <stdint.h>

static inline unsigned muniCountOnes(uint64_t bits)
{
    return (unsigned)__builtin_popcountll(bits);
}

// The index of the lowest set bit; bits must not be 0.
static inline unsigned muniLowestBit(uint64_t bits)
{
    return (unsigned)__builtin_ctzll(bits);
}

// The position of codePoint in a rank index (see src/tables.h): block is
// 1 plus the number of its 64-code-point block, or 0 for none. Returns -1
// when the index leaves codePoint out.
static inline int32_t muniRank(uint32_t block, uint32_t codePoint, const uint64_t* bits,
                               const uint16_t* ranks)
{
    if (block == 0)
    {
        return -1;
    }
    uint64_t map = bits[block - 1];
    unsigned bit = codePoint & 63;
    if ((map >> bit & 1) == 0)
    {
        return -1;
    }
    uint64_t below = bit == 0 ? 0 : map & (((uint64_t)1 << bit) - 1);
    return (int32_t)(ranks[block - 1] + muniCountOnes(below));
}

#endif // MAUL_UNICODE_SRC_BITS_H
