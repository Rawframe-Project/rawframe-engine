// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Sets of properties across every group (record mui-0004): one 64-bit
// word per group, as the public masks are, so a group's mask is a word.

#ifndef MAUL_UI_SRC_PROPERTY_BITS_H
#define MAUL_UI_SRC_PROPERTY_BITS_H

#include "maul-ui/style.h"

#include <stdbool.h>
#include <stdint.h>

enum
{
    // Groups of 64 ids, and so words.
    MUI_PROPERTY_GROUPS = 4,
    // The first id past every group.
    MUI_PROPERTY_LIMIT = 64 * MUI_PROPERTY_GROUPS,
};

typedef struct muiPropertyBits
{
    uint64_t words[MUI_PROPERTY_GROUPS];
} muiPropertyBits;

static inline muiPropertyBits muiPropertiesOf(muiPropertyGroup group, muiPropertyMask mask)
{
    muiPropertyBits bits = {0};
    bits.words[group] = mask;
    return bits;
}

static inline muiPropertyBits muiPropertyOf(muiProperty property)
{
    return muiPropertiesOf(MUI_PROPERTY_GROUP(property), MUI_PROPERTY_BIT(property));
}

static inline bool muiHasProperty(muiPropertyBits bits, muiProperty property)
{
    return (bits.words[MUI_PROPERTY_GROUP(property)] & MUI_PROPERTY_BIT(property)) != 0;
}

static inline bool muiAnyProperty(muiPropertyBits bits)
{
    uint64_t any = 0;
    for (uint32_t i = 0; i < MUI_PROPERTY_GROUPS; i++)
    {
        any |= bits.words[i];
    }
    return any != 0;
}

static inline muiPropertyBits muiUnion(muiPropertyBits a, muiPropertyBits b)
{
    for (uint32_t i = 0; i < MUI_PROPERTY_GROUPS; i++)
    {
        a.words[i] |= b.words[i];
    }
    return a;
}

static inline muiPropertyBits muiIntersection(muiPropertyBits a, muiPropertyBits b)
{
    for (uint32_t i = 0; i < MUI_PROPERTY_GROUPS; i++)
    {
        a.words[i] &= b.words[i];
    }
    return a;
}

// a without b's properties.
static inline muiPropertyBits muiWithout(muiPropertyBits a, muiPropertyBits b)
{
    for (uint32_t i = 0; i < MUI_PROPERTY_GROUPS; i++)
    {
        a.words[i] &= ~b.words[i];
    }
    return a;
}

static inline bool muiIntersects(muiPropertyBits a, muiPropertyBits b)
{
    return muiAnyProperty(muiIntersection(a, b));
}

// The lowest property of a set that is not empty, taken out of it: the
// lowest bit of its first word that is not 0, by a de Bruijn sequence.
static inline muiProperty muiTakeProperty(muiPropertyBits* bits)
{
    static const uint8_t index[64] = {
        0,  1,  48, 2,  57, 49, 28, 3,  61, 58, 50, 42, 38, 29, 17, 4,  62, 55, 59, 36, 53, 51,
        43, 22, 45, 39, 33, 30, 24, 18, 12, 5,  63, 47, 56, 27, 60, 41, 37, 16, 54, 35, 52, 21,
        44, 32, 23, 11, 46, 26, 40, 15, 34, 20, 31, 10, 25, 14, 19, 9,  13, 8,  7,  6};
    uint32_t group = 0;
    while (bits->words[group] == 0)
    {
        group++;
    }
    uint64_t word = bits->words[group];
    bits->words[group] = word & (word - 1);
    return (muiProperty)(group * 64 + index[((word & (~word + 1)) * 0x03F79D71B4CB0A89ull) >> 58]);
}

#endif // MAUL_UI_SRC_PROPERTY_BITS_H
