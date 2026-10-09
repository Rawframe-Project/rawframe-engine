// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Laid-out text as boxes (record mui-0006): a node's lines as hit
// testing and caret movement read them.

#ifndef MAUL_UI_SRC_TEXT_BOXES_H
#define MAUL_UI_SRC_TEXT_BOXES_H

#include "text_paragraph.h"

#include "maul-ui/text_edit.h"

#include <stdbool.h>
#include <stdint.h>

// A grapheme cluster on a line: from x0 to x1, the letter spacing after
// it included, and its bytes from start up to end.
typedef struct muiTextBox
{
    float x0;
    float x1;
    uint32_t start;
    uint32_t end;
    bool rtl;
} muiTextBox;

// A node's paragraph broken as painting breaks it, at a width.
typedef struct muiLaidText
{
    muiParagraph paragraph;
    const muiTextLine* lines;
    uint32_t lineCount;
    float width;
} muiLaidText;

// The boxes being written: the buffer, and how many it holds.
typedef struct muiTextBoxes
{
    muiTextService* service;
    const muiTextBlock* block;
    muiTextBox* data;
    uint32_t count;
    uint32_t capacity;
} muiTextBoxes;
// An edge of a line's boxes: the box's place, whether its right edge,
// and whether the text there runs right to left.
typedef struct muiTextEdge
{
    uint32_t box;
    bool right;
    bool rtl;
} muiTextEdge;

// Whether a host has its service and context.
bool muiIsTextHostValid(const muiTextHost* host);

// Lays a node's text out as painting does at a width, scrolled as
// muiFollowCaret leaves an editing block.
muiResult muiLayText(const muiTextHost* host, muiNodeId nodeId, float width, muiLaidText* out);

// Scrolls an editing block's laid-out text in a content box of the
// laid width and a height: from where it was, just far enough that the
// caret shows, then no further than the text reaches, so a shorter text
// comes back; the lines and the paragraph laid out scrolled with it.
// Text not editing stays where it is.
void muiFollowCaret(muiLaidText* laid, float height);

// Writes a line's boxes left to right into the service's scratch, valid
// until the next call; false when memory runs out.
bool muiGetLineBoxes(const muiLaidText* laid, uint32_t index, muiTextBoxes* out);

// The position at a box's left or right edge.
muiTextPosition muiEdgePosition(const muiTextBox* box, bool right);

// The line a position is on: the first whose next line starts after it,
// or the one before when it keeps upstream to that line's end.
uint32_t muiLineOfPosition(const muiLaidText* laid, muiTextPosition position);

// The position nearest x on a line: the nearer edge of the box at x;
// false when memory runs out.
bool muiHitLine(const muiLaidText* laid, uint32_t index, float x, muiTextPosition* out);

// The edge a position's caret is at on its line's boxes; false when the
// line has none.
bool muiFindEdge(const muiLaidText* laid, uint32_t line, const muiTextBoxes* boxes,
                 muiTextPosition position, muiTextEdge* out);

// The x of a position's caret on its line, and whether the text there
// runs right to left; on a line with no boxes, where the line starts.
// False when memory runs out.
bool muiCaretX(const muiLaidText* laid, uint32_t line, muiTextPosition position, float* xOut,
               bool* rtlOut);

// Finds the next stretch of side by side boxes wholly from start up to
// end, from box *at on: its left and right edges, *at moved past it;
// false when there is none.
bool muiNextStretch(const muiTextBoxes* boxes, uint32_t* at, uint32_t start, uint32_t end,
                    float* leftOut, float* rightOut);

#endif // MAUL_UI_SRC_TEXT_BOXES_H
