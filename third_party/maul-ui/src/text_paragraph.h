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
    // The block laid out, and the node's block it shows: a password's
    // mask, or the block itself (src/text_mask.h).
    muiTextBlock* block;
    muiTextBlock* source;
    muiFontChain chain;
    muiComputedTextStyle style;
    muiLineScale scale;
    float lineHeight;
    // From a line's top to its baseline.
    float baseline;
    // How far lines are scrolled left: an editing block's, once the caret
    // is followed (muiFollowCaret), else 0.
    float scrollX;
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

// How far a run's text reaches above and below its baseline: the
// font's ascent and descent at the run's size (its scale times the
// node's), with half its leading each way, as CSS places an inline box;
// an automatic line height is the font's, another scales with the run.
void muiRunReach(const muiParagraph* paragraph, const muiFontMetrics* metrics, float scale,
                 float* aboveOut, float* belowOut);

// Raises how far a line reaches above and below its baseline to what the
// spans' runs on it reach, shifted as they are; whether any reached past. Lines are taken in
// order, cursor (0 for the first) the first item not behind them.
bool muiLineReach(const muiParagraph* paragraph, const muiTextLine* line, uint32_t* cursor,
                  float* above, float* below);

// The height of a paragraph's lines, broken: the last one's bottom.
float muiParagraphHeight(const muiTextLine* lines, uint32_t count);

// The line at a height down a paragraph: the first above it, the last
// below them all.
uint32_t muiLineAtY(const muiTextLine* lines, uint32_t count, float y);

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
