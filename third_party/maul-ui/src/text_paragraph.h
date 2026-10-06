// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Paragraphs (record mui-0006): a node's text block laid out in its
// style, as measuring, painting and hit testing share it.

#ifndef MAUL_UI_SRC_TEXT_PARAGRAPH_H
#define MAUL_UI_SRC_TEXT_PARAGRAPH_H

#include "font_chain.h"
#include "line_break.h"
#include "text_block.h"
#include "text_service.h"
#include "text_shape.h"

#include "maul-ui/text_block.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// What laying out a node's block needs: the block, shaped, its font
// chain and the node's style, with the line scale and metrics in logical
// units, the first font's.
typedef struct muiParagraph
{
    muiTextService* service;
    muiTextBlock* block;
    muiFontChain chain;
    muiComputedTextStyle style;
    muiLineScale scale;
    float lineHeight;
    // From a line's top to its baseline.
    float baseline;
    bool rtl;
} muiParagraph;

// A line's glyphs and width: the block's, or, when the line breaks where
// shaping is unsafe to break, its own from shaping it alone.
typedef struct muiLineGlyphs
{
    const muiTextItem* items;
    uint32_t itemCount;
    const muiShapedGlyph* glyphs;
    uint32_t glyphCount;
    float width;
} muiLineGlyphs;

// Sets up a node's paragraph; false when there is nothing to lay out or
// its shaping found no memory, which the service counts.
bool muiPrepareParagraph(const muiTextHost* host, muiNodeId nodeId, uint64_t hostKey,
                         muiParagraph* out);

// Breaks a paragraph's lines into the service's scratch; false when
// there is no memory for them.
bool muiBreakParagraph(muiParagraph* paragraph, muiBreakMode mode, float width, uint32_t* countOut);

// How a paragraph breaks for a measure mode, painting breaking as
// mui_measureAtMost does.
muiBreakMode muiParagraphBreakMode(const muiParagraph* paragraph, muiMeasureMode mode);

// Logical units per unit of an item's font.
float muiItemScale(const muiParagraph* paragraph, const muiTextItem* item);

// A line's glyphs; false when shaping it alone found no memory, which the
// service counts.
bool muiGetLineGlyphs(const muiParagraph* paragraph, const muiTextLine* line, muiLineGlyphs* out);

// Where a line starts across a content box of the width.
float muiAlignLine(const muiParagraph* paragraph, float lineWidth, float width);

// The glyphs of an item, in visual order, whose clusters fall from start
// up to end: from *firstOut up to *lastOut.
void muiSegmentGlyphs(const muiShapedGlyph* glyphs, const muiTextItem* item, uint32_t start,
                      uint32_t end, uint32_t* firstOut, uint32_t* lastOut);

// A line's bidi runs left to right (UAX #9 L1 and L2), into the service's
// runs; false when memory runs out, which the service counts.
bool muiReorderLine(const muiParagraph* paragraph, const muiTextLine* line, size_t* countOut);

#endif // MAUL_UI_SRC_TEXT_PARAGRAPH_H
