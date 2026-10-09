// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// A block's lines kept between layouts, per break mode (record
// mui-0006): an edit's paragraphs are broken again, the lines after them
// moved; a new shaping, line scale or wrapping width breaks them whole.

#ifndef MAUL_UI_SRC_TEXT_LINES_H
#define MAUL_UI_SRC_TEXT_LINES_H

#include "line_break.h"
#include "text_paragraph.h"

#include <stdint.h>

// As muiBreakParagraph: a paragraph's lines in the service's scratch,
// placed down it; false when memory runs out.
bool muiLayLines(muiParagraph* paragraph, muiBreakMode mode, float width, uint32_t* countOut);

// The widest of a paragraph's lines' glyphs and the lines' height, the
// lines broken as muiLayLines breaks them; false when memory runs out.
bool muiMeasureLines(muiParagraph* paragraph, muiBreakMode mode, float width, float* widestOut,
                     float* heightOut);

#endif // MAUL_UI_SRC_TEXT_LINES_H
