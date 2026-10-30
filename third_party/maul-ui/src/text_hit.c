// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Hit testing laid-out text: points to positions, positions to carets, and
// ranges to rectangles, over a node's lines read as boxes of grapheme
// clusters.

#include "fpenv.h"
#include "text_boxes.h"
#include "text_entry.h"
#include "text_host.h"
#include "text_mask.h"

#include "maul-ui/text_edit.h"

#include <math.h>

muiResult muiHitText(const muiTextHost* host, muiNodeId nodeId, float width, float x, float y,
                     muiTextPosition* positionOut)
{
    if (!muiIsTextHostValid(host) || positionOut == nullptr || !isfinite(x) || !isfinite(y))
    {
        return mui_errorInvalid;
    }
    muiLaidText laid;
    muiResult result = muiLayText(host, nodeId, width, &laid);
    if (result != mui_success)
    {
        return result;
    }
    if (laid.lineCount == 0)
    {
        *positionOut = (muiTextPosition){0, mui_affinityDownstream};
        return mui_success;
    }
    uint32_t index = muiLineAtY(laid.lines, laid.lineCount, y);
    if (!muiHitLine(&laid, index, x, positionOut))
    {
        return mui_errorCapacity;
    }
    positionOut->offset = muiUnmaskOffset(laid.paragraph.source, positionOut->offset);
    return mui_success;
}

static MUI_NOINLINE muiResult TextHitTestWorker(const muiTextHost* host, muiNodeId nodeId,
                                                float width, float x, float y,
                                                muiTextPosition* positionOut)
{
    bool held = false;
    muiResult status = muiEnterTextHost(host, &held);
    if (status == mui_success)
    {
        status = muiHitText(host, nodeId, width, x, y, positionOut);
        muiLeaveTextHost(host, held);
    }
    return status;
}

muiResult muiNode_HitTestText(const muiTextHost* host, muiNodeId nodeId, float width, float x,
                              float y, muiTextPosition* positionOut)
{
    uint64_t floatEnv = muiFloatEnvEnter();
    muiResult result = TextHitTestWorker(host, nodeId, width, x, y, positionOut);
    muiFloatEnvLeave(floatEnv);
    return result;
}

muiResult muiCaretOf(const muiTextHost* host, muiNodeId nodeId, float width,
                     muiTextPosition position, muiTextCaret* caretOut)
{
    if (!muiIsTextHostValid(host) || caretOut == nullptr)
    {
        return mui_errorInvalid;
    }
    muiLaidText laid;
    muiResult result = muiLayText(host, nodeId, width, &laid);
    if (result != mui_success)
    {
        return result;
    }
    const muiParagraph* paragraph = &laid.paragraph;
    bool rtl = paragraph->rtl;
    float start = muiAlignLine(paragraph, 0.0f, width);
    if (laid.lineCount == 0)
    {
        *caretOut = (muiTextCaret){start, 0.0f, paragraph->lineHeight, rtl};
        return mui_success;
    }
    position.offset = muiMaskOffset(paragraph->source, position.offset);
    uint32_t index = muiLineOfPosition(&laid, position);
    float x = 0.0f;
    bool runRtl = false;
    if (!muiCaretX(&laid, index, position, &x, &runRtl))
    {
        return mui_errorCapacity;
    }
    const muiTextLine* line = &laid.lines[index];
    *caretOut = (muiTextCaret){x, line->top, line->height, runRtl};
    return mui_success;
}

static MUI_NOINLINE muiResult TextGetCaretWorker(const muiTextHost* host, muiNodeId nodeId,
                                                 float width, muiTextPosition position,
                                                 muiTextCaret* caretOut)
{
    bool held = false;
    muiResult status = muiEnterTextHost(host, &held);
    if (status == mui_success)
    {
        status = muiCaretOf(host, nodeId, width, position, caretOut);
        muiLeaveTextHost(host, held);
    }
    return status;
}

muiResult muiNode_GetTextCaret(const muiTextHost* host, muiNodeId nodeId, float width,
                               muiTextPosition position, muiTextCaret* caretOut)
{
    uint64_t floatEnv = muiFloatEnvEnter();
    muiResult result = TextGetCaretWorker(host, nodeId, width, position, caretOut);
    muiFloatEnvLeave(floatEnv);
    return result;
}

// Adds a rectangle for each stretch of a line's boxes from start up to
// end, those past capacity counted only; returns the count after them.
static uint32_t AddStretches(const muiTextBoxes* boxes, uint32_t start, uint32_t end, float top,
                             float height, muiRect* rects, uint32_t capacity, uint32_t count)
{
    uint32_t at = 0;
    float left = 0.0f;
    float right = 0.0f;
    while (muiNextStretch(boxes, &at, start, end, &left, &right))
    {
        if (count < capacity)
        {
            rects[count] = (muiRect){left, top, right - left, height};
        }
        count++;
    }
    return count;
}

static muiResult TextGetRangeRects(const muiTextHost* host, muiNodeId nodeId, float width,
                                   uint32_t start, uint32_t end, muiRect* rects, uint32_t capacity,
                                   uint32_t* countOut)
{
    if (!muiIsTextHostValid(host) || countOut == nullptr || (rects == nullptr && capacity != 0))
    {
        return mui_errorInvalid;
    }
    muiLaidText laid;
    muiResult result = muiLayText(host, nodeId, width, &laid);
    if (result != mui_success)
    {
        return result;
    }
    start = muiMaskOffset(laid.paragraph.source, start);
    end = muiMaskOffset(laid.paragraph.source, end);
    uint32_t count = 0;
    for (uint32_t index = 0; index < laid.lineCount && start < end; index++)
    {
        const muiTextLine* line = &laid.lines[index];
        if (line->end <= start || line->start >= end)
        {
            continue;
        }
        muiTextBoxes boxes;
        if (!muiGetLineBoxes(&laid, index, &boxes))
        {
            return mui_errorCapacity;
        }
        count = AddStretches(&boxes, start, end, line->top, line->height, rects, capacity, count);
    }
    *countOut = count;
    return count <= capacity ? mui_success : mui_errorCapacity;
}

static MUI_NOINLINE muiResult TextGetRangeRectsWorker(const muiTextHost* host, muiNodeId nodeId,
                                                      float width, uint32_t start, uint32_t end,
                                                      muiRect* rects, uint32_t capacity,
                                                      uint32_t* countOut)
{
    bool held = false;
    muiResult status = muiEnterTextHost(host, &held);
    if (status == mui_success)
    {
        status = TextGetRangeRects(host, nodeId, width, start, end, rects, capacity, countOut);
        muiLeaveTextHost(host, held);
    }
    return status;
}

muiResult muiNode_GetTextRangeRects(const muiTextHost* host, muiNodeId nodeId, float width,
                                    uint32_t start, uint32_t end, muiRect* rectsOut,
                                    uint32_t capacity, uint32_t* countOut)
{
    uint64_t floatEnv = muiFloatEnvEnter();
    muiResult result =
        TextGetRangeRectsWorker(host, nodeId, width, start, end, rectsOut, capacity, countOut);
    muiFloatEnvLeave(floatEnv);
    return result;
}
