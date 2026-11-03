// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// See word_kind.h.

#include "word_kind.h"

#include "tables.h"

#include "maul-unicode/properties.h"
#include "maul-unicode/segment.h"

uint8_t muniWordOtherKind(uint32_t codePoint)
{
    if (muniLookupIdeographic(codePoint) != 0)
    {
        return muni_wordIdeograph;
    }
    if (muniScriptTags[muniLookupScript(codePoint)] == MUNI_MAKE_SCRIPT('H', 'i', 'r', 'a'))
    {
        return muni_wordKana;
    }
    return muniLookupLineBreak(codePoint) == muni_lbSa ? muni_wordLetter : muni_wordNone;
}
