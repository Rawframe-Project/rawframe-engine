// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Shaping a text block (record mui-0006): bidi levels per paragraph from
// the base direction, the font of each grapheme cluster, items of one
// level, script and font, each shaped by HarfBuzz at its font's units per
// em with the whole text as context, and running sums of advances and
// clusters per byte for line breaking.

#ifndef MAUL_UI_SRC_TEXT_SHAPE_H
#define MAUL_UI_SRC_TEXT_SHAPE_H

#include "font_chain.h"
#include "font_store.h"
#include "text_block.h"
#include "text_service.h"

#include <stdbool.h>
#include <stdint.h>

// Shapes a block in a font chain and a base direction, unless it is
// shaped so already: each grapheme cluster in the first font of the chain
// that has all its characters, a cluster of characters of no one script
// staying in the font before it when that font has them; false when
// memory runs out, which leaves it unshaped.
bool muiShapeTextBlock(muiTextService* service, muiTextBlock* block, const muiFontChain* chain,
                       bool rtl);

// Whether a line breaking before the byte at offset would shape
// differently from the block: HarfBuzz marks the cluster there unsafe to
// break, or no cluster starts there, as inside a ligature.
bool muiIsBreakUnsafe(const muiTextBlock* block, uint32_t offset);

// A line shaped on its own: its pieces of the block's items, with their
// glyphs, in the buffers given.
typedef struct muiTextLineShape
{
    muiBuffer* items;
    uint32_t itemCount;
    muiBuffer* glyphs;
    uint32_t glyphCount;
} muiTextLineShape;

// Shapes bytes from start up to end of a block shaped in a chain alone,
// as a line broken there is, its items cut to them; false when memory
// runs out.
bool muiShapeTextLine(muiTextService* service, const muiTextBlock* block, const muiFontChain* chain,
                      uint32_t start, uint32_t end, muiTextLineShape* out);

#endif // MAUL_UI_SRC_TEXT_SHAPE_H
