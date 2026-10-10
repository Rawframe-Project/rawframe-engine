// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// A value text's marks (record mui-0008, I123): its selection, lines and
// words, checked against the text the build reads and the tree takes;
// and the boundaries of its units and its offsets in the units platforms
// count, which the adapters share (research 143).

#ifndef MAUL_UI_SRC_ACCESS_TEXT_H
#define MAUL_UI_SRC_ACCESS_TEXT_H

#include "maul-ui/access.h"
#include "maul-ui/access_tree.h"

#include <stdbool.h>
#include <stdint.h>

// Whether each part of marks fits a text of length bytes: offsets within
// it at the starts of characters; lines ascending from 0, words in order
// and apart, each with bytes in it. None given fits.
bool muiAccessSelectionFits(const muiAccessTextMarks* marks, const char* text, uint32_t length);
bool muiAccessLinesFit(const muiAccessTextMarks* marks, const char* text, uint32_t length);
bool muiAccessWordsFit(const muiAccessTextMarks* marks, const char* text, uint32_t length);

// Whether where marks show a text fits it: a box each line, tops above
// bottoms, first clusters ascending from 0, none for a line whose
// clusters are left out; clusters with bytes in them
// at the starts of characters, left edges before right ones; all finite.
// None given fits; clusters without line boxes do not.
bool muiAccessGeometryFits(const muiAccessTextMarks* marks, const char* text, uint32_t length);

// The rectangles of a byte range of a shown text in its node's own
// space, whose bounds are given: one each line, around its clusters with
// bytes in the range, or across the bounds for a line whose clusters are
// left out, those past capacity counted only; how many. The byte offset
// of the character at a point there: on the line whose box holds its y,
// or the nearest, the cluster whose edges hold its x, or the nearest; a
// line's start when it has none. Both need line boxes.
uint32_t muiAccessRangeRects(const muiAccessTextMarks* marks, muiRect bounds, uint32_t start,
                             uint32_t end, muiRect* rects, uint32_t capacity);
uint32_t muiAccessOffsetAt(const muiAccessTextMarks* marks, float x, float y);

// A byte range's rectangles where the root is placed, from the tree
// (muiAccessTree_GetTextRects): a few held here, more from the heap,
// let go with muiAccessFreeRects.
typedef struct muiAccessRects
{
    const muiAllocator* allocator;
    muiRect* rects;
    uint32_t count;
    uint32_t room;
    muiRect small[8];
} muiAccessRects;

// Gets a range's rectangles: `mui_success` (perhaps none), `mui_empty`
// when the node's text has no clusters or the node is not held,
// `mui_errorCapacity` when memory runs out; none but on success.
muiResult muiAccessGetRects(const muiAccessTree* tree, uint64_t id, uint32_t start, uint32_t end,
                            const muiAllocator* allocator, muiAccessRects* out);
void muiAccessFreeRects(muiAccessRects* rects);

// The rectangle of each UTF-16 unit of a node's value text from one up
// to a count, where the root is placed: its cluster's, a line high; a
// width of -1 for a unit no cluster holds or past the text. False for a
// node not held, text without clusters, or memory run out.
bool muiAccessUnitRects(const muiAccessTree* tree, uint64_t id, uint32_t first, uint32_t count,
                        const muiAllocator* allocator, muiRect* rectsOut);

// The box around a range's rectangles where the root is placed: the
// node's bounds when its text has no clusters and the range is not
// empty; false for none.
bool muiAccessTextBox(const muiAccessTree* tree, uint64_t id, uint32_t start, uint32_t end,
                      const muiAllocator* allocator, muiRect* boxOut);

// The units a value text is read and moved by.
typedef enum muiAccessUnit
{
    mui_unitCharacter,
    mui_unitWord,
    mui_unitLine,
    mui_unitParagraph,
    mui_unitDocument,
} muiAccessUnit;

// A node's value text as the boundaries read it: empty for none.
typedef struct muiAccessText
{
    const char* bytes;
    uint32_t length;
    const muiAccessTextMarks* marks;
} muiAccessText;

muiAccessText muiAccessValueOf(const muiAccessNode* node);

// Whether a node is a text input (a text input's roles, an editable
// combo box); whether the adapters give its value as text being edited:
// a text input's, or one with a selection.
bool muiAccessIsTextInput(const muiAccessNode* node);
bool muiAccessIsEdited(const muiAccessNode* node);

// The first boundary of a unit after a byte offset, the text's end past
// the last; the last before it, 0 before the first. A character is a
// code point; a word runs from its start to the next word's, as AT-SPI
// and UI Automation read words; a line from its start to the next's, a
// paragraph to just after its hard break (UAX #14's mandatory breaks);
// none given, a text is one line. The text's ends are boundaries of
// every unit.
uint32_t muiAccessBoundaryAfter(const muiAccessText* text, muiAccessUnit unit, uint32_t at);
uint32_t muiAccessBoundaryBefore(const muiAccessText* text, muiAccessUnit unit, uint32_t at);

// Counts of a text's code points and of its UTF-16 units before a byte
// offset; the byte offset where a count of either ends, clamped to the
// text, and at a character's start (a UTF-16 count inside a surrogate
// pair at the pair's).
uint32_t muiAccessPointsBefore(const char* text, uint32_t offset);
uint32_t muiAccessUtf16Before(const char* text, uint32_t offset);
uint32_t muiAccessByteOfPoints(const muiAccessText* text, uint32_t points);
uint32_t muiAccessByteOfUtf16(const muiAccessText* text, uint32_t units);

#endif // MAUL_UI_SRC_ACCESS_TEXT_H
