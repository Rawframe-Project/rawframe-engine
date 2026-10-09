// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Placing an editing block's selection through a node's laid-out text
// (record mui-0006): moves, presses and drags. A press selects a cluster
// edge, a word or a paragraph by its click count, as every platform's
// double and triple click; a drag extends by the same unit, keeping what
// the press selected. A move without Shift over a selection collapses it
// to the edge that way instead of moving on, as every platform's arrows.

#include "maul-ui/text_editor.h"

#include "text_editing.h"
#include "text_service.h"

#include "maul-ui/layout.h"
#include "maul-ui/text_edit.h"
#include "maul-unicode/segment.h"

#include <math.h>

// A node's editing block and its content box.
static muiResult EditingOf(const muiTextHost* host, muiNodeId nodeId, muiTextBlock** blockOut,
                           muiRect* contentOut)
{
    if (host == nullptr || host->service == nullptr || host->context == nullptr)
    {
        return muiRefuseText(host != nullptr ? host->service : nullptr);
    }
    uint64_t key = muiNode_GetHostKey(host->context, nodeId);
    muiTextBlockId blockId = {(uint32_t)key, (uint32_t)(key >> 32)};
    *contentOut = muiNode_GetContentRect(host->context, nodeId);
    return blockId.index1 != 0 ? muiEditBlock(host->service, blockId, blockOut)
                               : muiRefuseText(host->service);
}

static muiTextSelection Collapsed(muiTextPosition at)
{
    return (muiTextSelection){at.offset, at};
}

// A selection's edge a horizontal move goes to: by logical order for a
// cluster move, by where the carets are drawn for left and right.
static muiResult EdgeOf(const muiTextHost* host, muiNodeId nodeId, float width,
                        const muiTextSelection* selection, muiTextMovement movement,
                        muiTextPosition* edgeOut)
{
    muiTextPosition anchor = {selection->anchor, mui_affinityDownstream};
    bool caretFirst = selection->caret.offset < selection->anchor;
    bool towardEnd = movement == mui_moveNextCluster;
    if (movement == mui_moveLeft || movement == mui_moveRight)
    {
        muiTextCaret a;
        muiTextCaret b;
        muiResult result = muiTextGetCaret(host, nodeId, width, anchor, &a);
        result = result == mui_success ? muiTextGetCaret(host, nodeId, width, selection->caret, &b)
                                       : result;
        if (result != mui_success)
        {
            return result;
        }
        // Toward the end means toward the caret's side when it is first
        // to the right, as a left-to-right line has it.
        caretFirst = b.x < a.x || (b.x == a.x && caretFirst);
        towardEnd = movement == mui_moveRight;
    }
    *edgeOut = caretFirst == towardEnd ? anchor : selection->caret;
    return mui_success;
}

muiResult muiTextEditMove(const muiTextHost* host, muiNodeId nodeId, muiTextMovement movement,
                          bool extend)
{
    muiTextBlock* block = nullptr;
    muiRect content;
    muiResult result = movement <= mui_moveTextEnd
                           ? EditingOf(host, nodeId, &block, &content)
                           : muiRefuseText(host != nullptr ? host->service : nullptr);
    if (result != mui_success)
    {
        return result;
    }
    muiTextEditing* editing = &block->editing;
    muiTextSelection selection = editing->selection;
    bool vertical = movement == mui_moveLineUp || movement == mui_moveLineDown;
    bool cluster = movement <= mui_moveRight;
    muiTextPosition moved = selection.caret;
    if (!extend && cluster && selection.anchor != selection.caret.offset)
    {
        result = EdgeOf(host, nodeId, content.width, &selection, movement, &moved);
    }
    else
    {
        muiTextCaret caret;
        if (vertical && editing->preferredX < 0.0f)
        {
            result = muiTextGetCaret(host, nodeId, content.width, selection.caret, &caret);
            editing->preferredX = result == mui_success ? caret.x : -1.0f;
        }
        result = result == mui_success
                     ? muiTextMove(host, nodeId, content.width, selection.caret, movement,
                                   fmaxf(editing->preferredX, 0.0f), &moved)
                     : result;
    }
    if (result != mui_success)
    {
        return result;
    }
    muiPlaceSelection(block,
                      extend ? (muiTextSelection){selection.anchor, moved} : Collapsed(moved));
    muiEndPress(editing);
    if (!vertical)
    {
        editing->preferredX = -1.0f;
    }
    return mui_success;
}

// The word, or the white space between words, at an offset: a segment of
// Unicode's word boundaries; at the text's end, the one before it.
static void WordAround(const muiTextBlock* block, uint32_t at, uint32_t* startOut, uint32_t* endOut)
{
    muniSegmentIterator iterator;
    uint32_t start = 0;
    uint32_t end = block->length;
    if (muniInitWordIterator(&iterator, block->text.data, block->length, false) == muni_success)
    {
        size_t next = 0;
        while (muniNextSegmentBreak(&iterator, &next) == muni_success)
        {
            if (next > at || next == block->length)
            {
                end = (uint32_t)next;
                break;
            }
            start = (uint32_t)next;
        }
    }
    *startOut = start;
    *endOut = end;
}

static bool IsBreakByte(unsigned char byte)
{
    return byte >= '\n' && byte <= '\r';
}

// The length of the paragraph separator starting at a byte, or 0: LF,
// VT, FF and CR alone, NEL, and the line and paragraph separators
// U+2028 and U+2029.
static uint32_t SeparatorAt(const unsigned char* text, uint32_t length, uint32_t at)
{
    if (IsBreakByte(text[at]))
    {
        return 1;
    }
    if (text[at] == 0xC2 && at + 1 < length && text[at + 1] == 0x85)
    {
        return 2;
    }
    return text[at] == 0xE2 && at + 2 < length && text[at + 1] == 0x80 &&
                   (text[at + 2] == 0xA8 || text[at + 2] == 0xA9)
               ? 3
               : 0;
}

// The paragraph at an offset, without the separator ending it.
static void ParagraphAround(const muiTextBlock* block, uint32_t at, uint32_t* startOut,
                            uint32_t* endOut)
{
    const unsigned char* text = block->text.data;
    uint32_t end = at;
    while (end < block->length && SeparatorAt(text, block->length, end) == 0)
    {
        end++;
    }
    uint32_t start = 0;
    for (uint32_t i = 0; i < at;)
    {
        uint32_t size = SeparatorAt(text, block->length, i);
        i += size != 0 ? size : 1u;
        start = size != 0 ? i : start;
    }
    *startOut = start < at ? start : at;
    *endOut = end;
}

static void UnitAround(const muiTextBlock* block, uint8_t grain, uint32_t at, uint32_t* startOut,
                       uint32_t* endOut)
{
    if (grain == MUI_GRAIN_WORD && (block->editing.def.flags & mui_editPassword) != 0)
    {
        // A password is one word: its words stay unseen.
        *startOut = 0;
        *endOut = block->length;
    }
    else if (grain == MUI_GRAIN_WORD)
    {
        WordAround(block, at, startOut, endOut);
    }
    else if (grain == MUI_GRAIN_PARAGRAPH)
    {
        ParagraphAround(block, at, startOut, endOut);
    }
    else
    {
        *startOut = at;
        *endOut = at;
    }
}

// Extends the selection from what the press selected to the unit at a
// position.
static void ExtendTo(muiTextBlock* block, muiTextPosition position)
{
    muiTextEditing* editing = &block->editing;
    uint32_t start = 0;
    uint32_t end = 0;
    UnitAround(block, editing->grain, position.offset, &start, &end);
    muiTextSelection selection;
    if (position.offset < editing->pressStart)
    {
        muiTextPosition caret = {start, editing->grain == MUI_GRAIN_CLUSTER
                                            ? position.affinity
                                            : mui_affinityDownstream};
        selection = (muiTextSelection){editing->pressEnd, caret};
    }
    else if (end > editing->pressEnd)
    {
        muiTextPosition caret = {end, editing->grain == MUI_GRAIN_CLUSTER ? position.affinity
                                                                          : mui_affinityDownstream};
        selection = (muiTextSelection){editing->pressStart, caret};
    }
    else
    {
        selection =
            (muiTextSelection){editing->pressStart, {editing->pressEnd, mui_affinityDownstream}};
    }
    muiPlaceSelection(block, selection);
}

// The position at a point of a node's border box.
static muiResult PositionAt(const muiTextHost* host, muiNodeId nodeId, const muiRect* content,
                            float x, float y, muiTextPosition* positionOut)
{
    return isfinite(x) && isfinite(y) ? muiTextHitTest(host, nodeId, content->width, x - content->x,
                                                       y - content->y, positionOut)
                                      : muiRefuseText(host->service);
}

muiResult muiTextEditPress(const muiTextHost* host, muiNodeId nodeId, float x, float y,
                           uint32_t clickCount, bool extend)
{
    muiTextBlock* block = nullptr;
    muiRect content;
    muiTextPosition position = {0, mui_affinityDownstream};
    muiResult result = clickCount != 0 ? EditingOf(host, nodeId, &block, &content)
                                       : muiRefuseText(host != nullptr ? host->service : nullptr);
    result = result == mui_success ? PositionAt(host, nodeId, &content, x, y, &position) : result;
    if (result != mui_success)
    {
        return result;
    }
    muiTextEditing* editing = &block->editing;
    editing->grain = (uint8_t)((clickCount - 1) % 3);
    editing->preferredX = -1.0f;
    if (extend)
    {
        // From the anchor, as Shift and a press extend on every platform.
        editing->pressStart = editing->selection.anchor;
        editing->pressEnd = editing->selection.anchor;
        ExtendTo(block, position);
        return mui_success;
    }
    UnitAround(block, editing->grain, position.offset, &editing->pressStart, &editing->pressEnd);
    muiPlaceSelection(block, editing->grain == MUI_GRAIN_CLUSTER
                                 ? Collapsed(position)
                                 : (muiTextSelection){editing->pressStart,
                                                      {editing->pressEnd, mui_affinityDownstream}});
    return mui_success;
}

muiResult muiTextEditDrag(const muiTextHost* host, muiNodeId nodeId, float x, float y)
{
    muiTextBlock* block = nullptr;
    muiRect content;
    muiTextPosition position = {0, mui_affinityDownstream};
    muiResult result = EditingOf(host, nodeId, &block, &content);
    result = result == mui_success ? PositionAt(host, nodeId, &content, x, y, &position) : result;
    if (result == mui_success)
    {
        block->editing.preferredX = -1.0f;
        ExtendTo(block, position);
    }
    return result;
}
