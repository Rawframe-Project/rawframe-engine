// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Laid-out text as boxes (record mui-0006): each line laid out as
// painting lays it out, as boxes of grapheme clusters left to right, a
// glyph several clusters share cut into equal parts; positions are box
// edges.

#include "text_boxes.h"

#include "maul-unicode/bidi.h"
#include "maul-unicode/segment.h"

muiResult muiLayText(const muiTextHost* host, muiNodeId nodeId, float width, muiLaidText* out)
{
    uint64_t hostKey = muiNode_GetHostKey(host->context, nodeId);
    uint64_t failures = host->service->failures;
    if (!muiPrepareParagraph(host, nodeId, hostKey, &out->paragraph))
    {
        return host->service->failures != failures ? mui_errorCapacity : mui_errorStale;
    }
    muiParagraph* paragraph = &out->paragraph;
    if (!muiBreakParagraph(paragraph, muiParagraphBreakMode(paragraph, mui_measureAtMost), width,
                           &out->lineCount))
    {
        return mui_errorCapacity;
    }
    out->lines = paragraph->service->lines.data;
    out->width = width;
    return mui_success;
}

enum
{
    // The most grapheme clusters a glyph's cluster is cut into; one with
    // more is one box.
    MAX_PARTS = 32
};

// Adds a cluster's box, cut into one per grapheme cluster when a glyph
// spans several, left to right: for right-to-left text the last in the
// text first.
static void AddCluster(muiTextBoxes* boxes, float x0, float x1, uint32_t start, uint32_t end,
                       bool rtl)
{
    const char* text = boxes->block->text.data;
    // bounds[k] up to bounds[k + 1] are part k's bytes.
    uint32_t bounds[MAX_PARTS + 1] = {start, end};
    uint32_t parts = 1;
    size_t breaks[MAX_PARTS];
    size_t found = 0;
    if (end - start > 1 &&
        muniFindGraphemeBreaks(text + start, end - start, breaks, MAX_PARTS, &found) ==
            muni_success &&
        found > 1)
    {
        parts = (uint32_t)found;
        for (uint32_t k = 0; k < parts; k++)
        {
            bounds[k + 1] = start + (uint32_t)breaks[k];
        }
    }
    float share = (x1 - x0) / (float)parts;
    for (uint32_t v = 0; v < parts && boxes->count < boxes->capacity; v++)
    {
        uint32_t k = rtl ? parts - 1 - v : v;
        float left = x0 + share * (float)v;
        float right = v + 1 == parts ? x1 : x0 + share * (float)(v + 1);
        boxes->data[boxes->count++] = (muiTextBox){left, right, bounds[k], bounds[k + 1], rtl};
    }
}

// Adds the boxes of an item's clusters from start up to end, from pen x;
// returns the pen after them.
static float AddSegment(const muiParagraph* paragraph, const muiLineGlyphs* source,
                        const muiTextItem* item, uint32_t start, uint32_t end, float pen,
                        muiTextBoxes* boxes)
{
    const muiShapedGlyph* shaped = source->glyphs + item->firstGlyph;
    float scale = muiItemScale(paragraph, item);
    bool rtl = (item->level & 1) != 0;
    uint32_t first = 0;
    uint32_t last = 0;
    muiSegmentGlyphs(shaped, item, start, end, &first, &last);
    // In right-to-left text the cluster after one in the text is to its
    // left: the one last added.
    uint32_t left = end;
    for (uint32_t i = first; i < last;)
    {
        uint32_t cluster = shaped[i].cluster;
        float x0 = pen;
        for (; i < last && shaped[i].cluster == cluster; i++)
        {
            pen += (float)shaped[i].advance * scale;
        }
        pen += paragraph->scale.spacing;
        uint32_t following = rtl ? left : (i < last ? shaped[i].cluster : end);
        AddCluster(boxes, x0, pen, cluster, following, rtl);
        left = cluster;
    }
    return pen;
}

// Writes a line's boxes left to right into the service's scratch; false
// when memory runs out.
bool muiGetLineBoxes(const muiLaidText* laid, uint32_t index, muiTextBoxes* out)
{
    const muiParagraph* paragraph = &laid->paragraph;
    const muiTextLine* line = &laid->lines[index];
    muiTextService* service = paragraph->service;
    muiLineGlyphs source;
    size_t runCount = 0;
    // A box for each byte at most, as each holds one or more.
    size_t capacity = (size_t)(line->end - line->start) + 1u;
    if (!muiGetLineGlyphs(paragraph, line, &source) ||
        !muiReserve(&service->allocator, &service->hitBoxes, capacity * sizeof(muiTextBox)) ||
        !muiReorderLine(paragraph, line, &runCount))
    {
        return false;
    }
    *out = (muiTextBoxes){service, paragraph->block, service->hitBoxes.data, 0, (uint32_t)capacity};
    const muniBidiRun* runs = service->runs.data;
    float pen = muiAlignLine(paragraph, source.width, laid->width);
    for (size_t r = 0; r < runCount; r++)
    {
        uint32_t start = line->start + (uint32_t)runs[r].start;
        uint32_t end = start + (uint32_t)runs[r].length;
        bool odd = (runs[r].level & 1) != 0;
        for (uint32_t k = 0; k < source.itemCount; k++)
        {
            const muiTextItem* item = &source.items[odd ? source.itemCount - 1 - k : k];
            if (item->end <= start || item->start >= end)
            {
                continue;
            }
            uint32_t from = item->start > start ? item->start : start;
            uint32_t to = item->end < end ? item->end : end;
            pen = AddSegment(paragraph, &source, item, from, to, pen, out);
        }
    }
    return true;
}

// The position at a box's left or right edge.
muiTextPosition muiEdgePosition(const muiTextBox* box, bool right)
{
    // The edge a cluster starts at in its direction is its start.
    bool leading = right == box->rtl;
    return leading ? (muiTextPosition){box->start, mui_affinityDownstream}
                   : (muiTextPosition){box->end, mui_affinityUpstream};
}

bool muiIsTextHostValid(const muiTextHost* host)
{
    return host != nullptr && host->service != nullptr && host->context != nullptr;
}

// The line a position is on: the first whose next line starts after it,
// or the one before when it keeps upstream to that line's end.
uint32_t muiLineOfPosition(const muiLaidText* laid, muiTextPosition position)
{
    uint32_t index = 0;
    while (index + 1 < laid->lineCount && position.offset >= laid->lines[index].next)
    {
        index++;
    }
    bool upstream = position.affinity == mui_affinityUpstream;
    if (upstream && index > 0 && position.offset == laid->lines[index].start)
    {
        index--;
    }
    return index;
}

bool muiFindEdge(const muiLaidText* laid, uint32_t line, const muiTextBoxes* boxes,
                 muiTextPosition position, muiTextEdge* out)
{
    if (boxes->count == 0)
    {
        return false;
    }
    // Downstream: the leading edge of the box the offset is in;
    // upstream: the trailing edge of the one it ends; the other when
    // that has none.
    bool upstream = position.affinity == mui_affinityUpstream;
    for (int pass = 0; pass < 2; pass++, upstream = !upstream)
    {
        for (uint32_t i = 0; i < boxes->count; i++)
        {
            const muiTextBox* box = &boxes->data[i];
            bool at = upstream ? box->end == position.offset
                               : box->start <= position.offset && position.offset < box->end;
            if (at)
            {
                *out = (muiTextEdge){i, upstream != box->rtl, box->rtl};
                return true;
            }
        }
    }
    // Past the clusters: the line's end in the paragraph's direction, or
    // its start before them.
    bool rtl = laid->paragraph.rtl;
    bool atEnd = position.offset >= laid->lines[line].end;
    bool left = atEnd == rtl;
    *out = left ? (muiTextEdge){0, false, rtl} : (muiTextEdge){boxes->count - 1, true, rtl};
    return true;
}

bool muiHitLine(const muiLaidText* laid, uint32_t index, float x, muiTextPosition* out)
{
    muiTextBoxes boxes;
    if (!muiGetLineBoxes(laid, index, &boxes))
    {
        return false;
    }
    if (boxes.count == 0)
    {
        *out = (muiTextPosition){laid->lines[index].start, mui_affinityDownstream};
        return true;
    }
    // The last box starting at or before x, else the first; past its
    // middle, its right edge.
    const muiTextBox* box = &boxes.data[0];
    for (uint32_t i = 1; i < boxes.count && x >= boxes.data[i].x0; i++)
    {
        box = &boxes.data[i];
    }
    bool right = x - box->x0 >= box->x1 - x;
    *out = muiEdgePosition(box, right);
    return true;
}

bool muiCaretX(const muiLaidText* laid, uint32_t line, muiTextPosition position, float* xOut,
               bool* rtlOut)
{
    muiTextBoxes boxes;
    if (!muiGetLineBoxes(laid, line, &boxes))
    {
        return false;
    }
    const muiParagraph* paragraph = &laid->paragraph;
    *xOut = muiAlignLine(paragraph, 0.0f, laid->width);
    *rtlOut = paragraph->rtl;
    muiTextEdge edge;
    if (muiFindEdge(laid, line, &boxes, position, &edge))
    {
        const muiTextBox* box = &boxes.data[edge.box];
        *xOut = edge.right ? box->x1 : box->x0;
        *rtlOut = edge.rtl;
    }
    return true;
}

bool muiNextStretch(const muiTextBoxes* boxes, uint32_t* at, uint32_t start, uint32_t end,
                    float* leftOut, float* rightOut)
{
    uint32_t i = *at;
    while (i < boxes->count && (boxes->data[i].start < start || boxes->data[i].end > end))
    {
        i++;
    }
    bool found = i < boxes->count;
    if (found)
    {
        *leftOut = boxes->data[i].x0;
    }
    while (i < boxes->count && boxes->data[i].start >= start && boxes->data[i].end <= end)
    {
        *rightOut = boxes->data[i].x1;
        i++;
    }
    *at = i;
    return found;
}
