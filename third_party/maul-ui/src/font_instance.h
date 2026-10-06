// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Font keys and the instances they name (record mui-0006). A key's low 16
// bits are its font's slot less one, the next 24 the slot's generation,
// and the top 24 an instance of the font: what a text style's weight,
// slant and size made of it, already decided against the font's axes and
// styles, so that whoever holds the key rebuilds the same axes and
// synthesis. Instance 0 is the font as it is.

#ifndef MAUL_UI_SRC_FONT_INSTANCE_H
#define MAUL_UI_SRC_FONT_INSTANCE_H

#include "font_store.h"

#include "maul-ui/text_style.h"

#include <stdbool.h>
#include <stdint.h>

// The bits of a key that name its font or family.
#define MUI_FONT_PART_MASK 0xFFFFFFFFFFull

// The bit that makes a key a font family's.
#define MUI_FAMILY_BIT (1ull << 63)

// What an instance does, decoded from its bits.
typedef struct muiInstance
{
    // The wght axis's value; 0 for its default.
    float weight;
    // The opsz axis's value; 0 for its default.
    float opticalSize;
    // ital at 1, slnt at -14 (within its range), or outlines sheared by a
    // quarter of their height.
    bool italic;
    bool slanted;
    bool sheared;
    // Outlines grown by an em/24 and advances with them.
    bool emboldened;
} muiInstance;

// The key of a font's slot and generation, at instance 0.
uint64_t muiKeyOf(uint32_t slot, uint32_t generation);

// The generation a key keeps of a slot's.
uint32_t muiKeyGeneration(uint32_t generation);

// The instance a style's weight (1 to 1000), slant and size in logical
// units make of a font, as a key's top 24 bits.
uint64_t muiInstanceBits(const muiFont* font, float weight, muiFontSlant slant, float size);

muiInstance muiDecodeInstance(uint64_t key);

// Writes the design coordinates of a font's axes, in 16.16, for an
// instance; returns how many, at most MUI_MAX_FONT_AXES.
uint32_t muiInstanceCoordinates(const muiFont* font, const muiInstance* instance,
                                FT_Fixed* coordinates);

// The HarfBuzz font that shapes a key's instance of a font, made and kept
// in a few slots of the font's; NULL when memory runs out. It lives until
// the font is destroyed or more instances are shaped.
hb_font_t* muiShapingFontOf(muiFont* font, uint64_t key);

#endif // MAUL_UI_SRC_FONT_INSTANCE_H
