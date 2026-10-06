// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The fonts a text style draws with, in the order they are tried for
// each character (record mui-0006): the face its font or family gives,
// then the family's fallbacks, then the service's, each a font or a
// family's face, in the instance the style makes of it.

#ifndef MAUL_UI_SRC_FONT_CHAIN_H
#define MAUL_UI_SRC_FONT_CHAIN_H

#include "font_store.h"
#include "text_service.h"

#include "maul-ui/text_style.h"

#include <stdbool.h>
#include <stdint.h>

enum
{
    // The first font and the most fallbacks of a family and of the
    // service.
    MUI_MAX_CHAIN = 1 + 2 * MUI_MAX_FALLBACKS
};

typedef struct muiFontChain
{
    muiFont* fonts[MUI_MAX_CHAIN];
    // Each font's key, naming its instance.
    uint64_t keys[MUI_MAX_CHAIN];
    uint32_t count;
    // What tells one chain from another, made of the keys.
    uint64_t identity;
} muiFontChain;

// The chain of a style; false when its font or family names none, or
// leaves no face.
bool muiBuildChain(const muiTextService* service, const muiComputedTextStyle* style,
                   muiFontChain* out);

#endif // MAUL_UI_SRC_FONT_CHAIN_H
