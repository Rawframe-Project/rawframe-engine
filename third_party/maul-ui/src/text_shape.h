// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Shaping a text block (record mui-0006): bidi levels per paragraph from
// the base direction, items of one level and script, each shaped by
// HarfBuzz at the font's units per em with the whole text as context,
// and running sums of advances and clusters per byte for line breaking.

#ifndef MAUL_UI_SRC_TEXT_SHAPE_H
#define MAUL_UI_SRC_TEXT_SHAPE_H

#include "font_store.h"
#include "text_block.h"
#include "text_service.h"

#include <stdbool.h>
#include <stdint.h>

// Shapes a block in a font, named by fontKey, and a base direction,
// unless it is shaped so already; false when memory runs out, which
// leaves it unshaped.
bool muiShapeTextBlock(muiTextService* service, muiTextBlock* block, const muiFont* font,
                       uint64_t fontKey, bool rtl);

#endif // MAUL_UI_SRC_TEXT_SHAPE_H
