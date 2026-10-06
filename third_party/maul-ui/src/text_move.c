// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Moving positions through laid-out text (record mui-0006): by grapheme
// cluster in the text or on screen, by word, and by line.

#include "text_boxes.h"

#include "maul-ui/text_edit.h"
#include "maul-unicode/encoding.h"
#include "maul-unicode/properties.h"
#include "maul-unicode/segment.h"

#include <math.h>

static muiTextPosition Downstream(uint32_t offset)
{
    return (muiTextPosition){offset, mui_affinityDownstream};
}

// The grapheme cluster boundary after offset, or the one before it.
static uint32_t ClusterBoundary(const muiTextBlock* block, uint32_t offset, bool next)
{
    muniSegmentIterator iterator;
    (void)muniInitGraphemeIterator(&iterator, block->text.data, block->length, false);
    uint32_t before = 0;
    size_t at = 0;
    while (muniNextSegmentBreak(&iterator, &at) == muni_success)
    {
        if (next ? at > offset : at >= offset)
        {
            return next ? (uint32_t)at : before;
        }
        before = (uint32_t)at;
    }
    // Past the last boundary, the text's end, for either direction.
    return before;
}

// Whether text from start up to end has a letter or a number.
static bool IsWord(const char* text, size_t start, size_t end)
{
    for (size_t at = start; at < end;)
    {
        uint32_t point = 0;
        size_t size = 1;
        (void)muniDecodeUtf8(text + at, end - at, &point, &size);
        muniGeneralCategory category = muniGetGeneralCategory(point);
        if ((category >= muni_gcLu && category <= muni_gcLo) ||
            (category >= muni_gcNd && category <= muni_gcNo))
        {
            return true;
        }
        at += size;
    }
    return false;
}

// Where a word movement from offset goes, through the word segments.
static uint32_t WordBoundary(const muiTextBlock* block, uint32_t offset, muiTextMovement movement)
{
    const char* text = block->text.data;
    muniSegmentIterator iterator;
    (void)muniInitWordIterator(&iterator, text, block->length, false);
    bool back = movement == mui_movePreviousWordStart;
    uint32_t found = back ? 0 : block->length;
    size_t start = 0;
    size_t end = 0;
    while ((!back || start < offset) && muniNextSegmentBreak(&iterator, &end) == muni_success)
    {
        if (IsWord(text, start, end))
        {
            if (back)
            {
                found = (uint32_t)start;
            }
            else if (movement == mui_moveNextWordStart ? start > offset : end > offset)
            {
                return (uint32_t)(movement == mui_moveNextWordStart ? start : end);
            }
        }
        start = end;
    }
    return found;
}

// Across a line's end: forward to the next line's start, back to the end
// of the line before; at the text's ends, from stays.
static muiTextPosition CrossLine(const muiLaidText* laid, uint32_t line, bool forward,
                                 muiTextPosition from)
{
    if (forward)
    {
        return line + 1 < laid->lineCount ? Downstream(laid->lines[line + 1].start) : from;
    }
    return line > 0 ? (muiTextPosition){laid->lines[line - 1].end, mui_affinityUpstream} : from;
}

// One cluster left or right on screen: from a box's one edge to its
// other, or from the edge the boxes share to the next box's far edge.
static bool MoveOnScreen(const muiLaidText* laid, muiTextPosition from, bool right,
                         muiTextPosition* out)
{
    uint32_t line = muiLineOfPosition(laid, from);
    bool forward = right != laid->paragraph.rtl;
    muiTextBoxes boxes;
    if (!muiGetLineBoxes(laid, line, &boxes))
    {
        return false;
    }
    muiTextEdge edge;
    if (!muiFindEdge(laid, line, &boxes, from, &edge))
    {
        *out = CrossLine(laid, line, forward, from);
        return true;
    }
    uint32_t box = edge.box;
    if (edge.right == right)
    {
        if (right ? box + 1 >= boxes.count : box == 0)
        {
            *out = CrossLine(laid, line, forward, from);
            return true;
        }
        box = right ? box + 1 : box - 1;
    }
    *out = muiEdgePosition(&boxes.data[box], right);
    return true;
}

// The line above or below at an x: from's caret x when x is NaN.
static bool MoveVertically(const muiLaidText* laid, muiTextPosition from, bool down, float x,
                           muiTextPosition* out)
{
    uint32_t line = muiLineOfPosition(laid, from);
    if (down ? line + 1 >= laid->lineCount : line == 0)
    {
        *out = Downstream(down ? laid->paragraph.block->length : 0);
        return true;
    }
    bool rtl = false;
    if (isnan(x) && !muiCaretX(laid, line, from, &x, &rtl))
    {
        return false;
    }
    return muiHitLine(laid, down ? line + 1 : line - 1, x, out);
}

// The start or end of from's line.
static muiTextPosition LineEdge(const muiLaidText* laid, muiTextPosition from, bool end)
{
    const muiTextLine* line = &laid->lines[muiLineOfPosition(laid, from)];
    // The end keeps upstream to the line, unless the line is empty and
    // its end is also its start.
    return end && line->end > line->start ? (muiTextPosition){line->end, mui_affinityUpstream}
                                          : Downstream(end ? line->end : line->start);
}

static bool Move(const muiLaidText* laid, muiTextPosition from, muiTextMovement movement,
                 float preferredX, muiTextPosition* out)
{
    const muiTextBlock* block = laid->paragraph.block;
    switch (movement)
    {
    case mui_moveNextCluster:
    case mui_movePreviousCluster:
        *out = Downstream(ClusterBoundary(block, from.offset, movement == mui_moveNextCluster));
        return true;
    case mui_moveLeft:
    case mui_moveRight:
        return MoveOnScreen(laid, from, movement == mui_moveRight, out);
    case mui_moveNextWordStart:
    case mui_moveNextWordEnd:
    case mui_movePreviousWordStart:
        *out = Downstream(WordBoundary(block, from.offset, movement));
        return true;
    case mui_moveLineStart:
    case mui_moveLineEnd:
        *out = LineEdge(laid, from, movement == mui_moveLineEnd);
        return true;
    case mui_moveLineUp:
    case mui_moveLineDown:
        return MoveVertically(laid, from, movement == mui_moveLineDown, preferredX, out);
    default:
        *out = Downstream(movement == mui_moveTextEnd ? block->length : 0);
        return true;
    }
}

muiResult muiTextMove(const muiTextHost* host, muiNodeId nodeId, float width, muiTextPosition from,
                      muiTextMovement movement, float preferredX, muiTextPosition* positionOut)
{
    if (!muiIsTextHostValid(host) || positionOut == nullptr || movement > mui_moveTextEnd)
    {
        return mui_errorInvalid;
    }
    muiLaidText laid;
    muiResult result = muiLayText(host, nodeId, width, &laid);
    if (result != mui_success)
    {
        return result;
    }
    uint32_t length = laid.paragraph.block->length;
    if (laid.lineCount == 0)
    {
        *positionOut = Downstream(0);
        return mui_success;
    }
    from.offset = from.offset < length ? from.offset : length;
    return Move(&laid, from, movement, preferredX, positionOut) ? mui_success : mui_errorCapacity;
}
