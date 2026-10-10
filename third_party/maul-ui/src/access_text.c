// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// A value text's marks, checked, and its boundaries and offsets (record
// mui-0008, I123, research 143).

#include "access_text.h"

#include "allocator.h"

#include <math.h>
#include <stdalign.h>

// Whether offset is within the text at a character's start: the end, or
// a byte that does not continue a UTF-8 sequence.
static bool IsStart(const char* text, uint32_t length, uint32_t offset)
{
    return offset == length || (offset < length && ((unsigned char)text[offset] & 0xC0u) != 0x80u);
}

bool muiAccessSelectionFits(const muiAccessTextMarks* marks, const char* text, uint32_t length)
{
    return !marks->selected ||
           (IsStart(text, length, marks->anchor) && IsStart(text, length, marks->focus));
}

bool muiAccessLinesFit(const muiAccessTextMarks* marks, const char* text, uint32_t length)
{
    if (marks->lineCount == 0)
    {
        return true;
    }
    if (marks->lineStarts == nullptr || marks->lineStarts[0] != 0)
    {
        return false;
    }
    for (uint32_t i = 1; i < marks->lineCount; i++)
    {
        uint32_t start = marks->lineStarts[i];
        if (start <= marks->lineStarts[i - 1] || !IsStart(text, length, start))
        {
            return false;
        }
    }
    return true;
}

bool muiAccessWordsFit(const muiAccessTextMarks* marks, const char* text, uint32_t length)
{
    if (marks->wordCount != 0 && marks->words == nullptr)
    {
        return false;
    }
    uint32_t after = 0;
    for (uint32_t i = 0; i < marks->wordCount; i++)
    {
        muiAccessWord word = marks->words[i];
        if (word.start < after || word.end <= word.start || !IsStart(text, length, word.start) ||
            !IsStart(text, length, word.end))
        {
            return false;
        }
        after = word.end;
    }
    return true;
}

// Two edges, finite and in order.
static bool IsSpan(float low, float high)
{
    return isfinite(low) && isfinite(high) && low <= high;
}

static bool LineBoxesFit(const muiAccessTextMarks* marks)
{
    uint32_t first = 0;
    for (uint32_t i = 0; i < marks->lineCount; i++)
    {
        const muiAccessLineBox* box = &marks->lineBoxes[i];
        uint32_t next =
            i + 1 < marks->lineCount ? marks->lineBoxes[i + 1].firstCluster : marks->clusterCount;
        if (!IsSpan(box->top, box->bottom) || box->firstCluster < first ||
            box->firstCluster > marks->clusterCount || (i == 0 && box->firstCluster != 0) ||
            (box->omitted && next != box->firstCluster))
        {
            return false;
        }
        first = box->firstCluster;
    }
    return true;
}

bool muiAccessGeometryFits(const muiAccessTextMarks* marks, const char* text, uint32_t length)
{
    if (marks->lineBoxes == nullptr)
    {
        return marks->clusterCount == 0;
    }
    if (marks->lineCount == 0 || (marks->clusterCount != 0 && marks->clusters == nullptr) ||
        !LineBoxesFit(marks))
    {
        return false;
    }
    for (uint32_t i = 0; i < marks->clusterCount; i++)
    {
        const muiAccessCluster* cluster = &marks->clusters[i];
        if (cluster->end <= cluster->start || !IsStart(text, length, cluster->start) ||
            !IsStart(text, length, cluster->end) || !IsSpan(cluster->left, cluster->right))
        {
            return false;
        }
    }
    return true;
}

// A line's clusters: from its first up to the next line's.
static uint32_t EndOfLine(const muiAccessTextMarks* marks, uint32_t line)
{
    return line + 1 < marks->lineCount ? marks->lineBoxes[line + 1].firstCluster
                                       : marks->clusterCount;
}

// Whether a range has bytes of a line.
static bool Touches(const muiAccessTextMarks* marks, uint32_t line, uint32_t start, uint32_t end)
{
    uint32_t from = marks->lineStarts != nullptr ? marks->lineStarts[line] : 0;
    uint32_t to = line + 1 < marks->lineCount && marks->lineStarts != nullptr
                      ? marks->lineStarts[line + 1]
                      : UINT32_MAX;
    return start < to && end > from;
}

uint32_t muiAccessRangeRects(const muiAccessTextMarks* marks, muiRect bounds, uint32_t start,
                             uint32_t end, muiRect* rects, uint32_t capacity)
{
    uint32_t count = 0;
    for (uint32_t line = 0; line < marks->lineCount && start < end; line++)
    {
        float left = INFINITY;
        float right = -INFINITY;
        if (marks->lineBoxes[line].omitted && Touches(marks, line, start, end))
        {
            left = bounds.x;
            right = bounds.x + bounds.width;
        }
        for (uint32_t i = marks->lineBoxes[line].firstCluster; i < EndOfLine(marks, line); i++)
        {
            const muiAccessCluster* cluster = &marks->clusters[i];
            if (cluster->start < end && cluster->end > start)
            {
                left = cluster->left < left ? cluster->left : left;
                right = cluster->right > right ? cluster->right : right;
            }
        }
        if (left <= right)
        {
            const muiAccessLineBox* box = &marks->lineBoxes[line];
            if (count < capacity)
            {
                rects[count] = (muiRect){left, box->top, right - left, box->bottom - box->top};
            }
            count++;
        }
    }
    return count;
}

// How far a value lies outside a span; 0 inside it.
static float Outside(float value, float low, float high)
{
    return value < low ? low - value : value > high ? value - high : 0.0f;
}

uint32_t muiAccessOffsetAt(const muiAccessTextMarks* marks, float x, float y)
{
    uint32_t line = 0;
    for (uint32_t i = 1; i < marks->lineCount; i++)
    {
        const muiAccessLineBox* box = &marks->lineBoxes[i];
        const muiAccessLineBox* best = &marks->lineBoxes[line];
        if (Outside(y, box->top, box->bottom) < Outside(y, best->top, best->bottom))
        {
            line = i;
        }
    }
    uint32_t first = marks->lineBoxes[line].firstCluster;
    uint32_t last = EndOfLine(marks, line);
    if (first == last || marks->lineBoxes[line].omitted)
    {
        return marks->lineStarts != nullptr ? marks->lineStarts[line] : 0;
    }
    uint32_t nearest = first;
    for (uint32_t i = first + 1; i < last; i++)
    {
        const muiAccessCluster* cluster = &marks->clusters[i];
        const muiAccessCluster* best = &marks->clusters[nearest];
        if (Outside(x, cluster->left, cluster->right) < Outside(x, best->left, best->right))
        {
            nearest = i;
        }
    }
    return marks->clusters[nearest].start;
}

muiResult muiAccessGetRects(const muiAccessTree* tree, uint64_t id, uint32_t start, uint32_t end,
                            const muiAllocator* allocator, muiAccessRects* out)
{
    *out = (muiAccessRects){.allocator = allocator};
    out->rects = out->small;
    uint32_t count = 0;
    muiResult status = muiAccessTree_GetTextRects(tree, id, start, end, nullptr, 0, &count);
    if (status != mui_success && status != mui_errorCapacity)
    {
        return status == mui_empty ? mui_empty : mui_errorCapacity;
    }
    if (count > 8)
    {
        out->rects = muiAllocate(allocator, count * sizeof(muiRect), alignof(muiRect));
        if (out->rects == nullptr)
        {
            out->rects = out->small;
            return mui_errorCapacity;
        }
        out->room = count;
    }
    status = muiAccessTree_GetTextRects(tree, id, start, end, out->rects, count, &out->count);
    if (status != mui_success)
    {
        muiAccessFreeRects(out);
        return mui_errorCapacity;
    }
    return mui_success;
}

void muiAccessFreeRects(muiAccessRects* rects)
{
    if (rects->rects != rects->small && rects->rects != nullptr)
    {
        muiRelease(rects->allocator, rects->rects, rects->room * sizeof(muiRect), alignof(muiRect));
    }
    rects->rects = rects->small;
    rects->count = 0;
    rects->room = 0;
}

bool muiAccessTextBox(const muiAccessTree* tree, uint64_t id, uint32_t start, uint32_t end,
                      const muiAllocator* allocator, muiRect* boxOut)
{
    muiAccessRects got;
    muiResult status = muiAccessGetRects(tree, id, start, end, allocator, &got);
    if (status == mui_empty)
    {
        return start < end && muiAccessTree_GetBounds(tree, id, boxOut) == mui_success;
    }
    float left = INFINITY;
    float top = INFINITY;
    float right = -INFINITY;
    float bottom = -INFINITY;
    for (uint32_t i = 0; i < got.count; i++)
    {
        const muiRect* r = &got.rects[i];
        left = fminf(left, r->x);
        top = fminf(top, r->y);
        right = fmaxf(right, r->x + r->width);
        bottom = fmaxf(bottom, r->y + r->height);
    }
    bool found = got.count != 0;
    muiAccessFreeRects(&got);
    *boxOut = (muiRect){left, top, right - left, bottom - top};
    return found;
}

muiAccessText muiAccessValueOf(const muiAccessNode* node)
{
    const char* bytes = node->text[mui_accessValue];
    return (muiAccessText){bytes != nullptr ? bytes : "",
                           bytes != nullptr ? node->textLength[mui_accessValue] : 0, &node->marks};
}

bool muiAccessIsTextInput(const muiAccessNode* node)
{
    return (node->role >= mui_roleTextInput && node->role <= mui_roleUrlInput) ||
           node->role == mui_roleEditableComboBox;
}

bool muiAccessIsEdited(const muiAccessNode* node)
{
    return muiAccessIsTextInput(node) || node->marks.selected;
}

static bool IsLead(char byte)
{
    return ((unsigned char)byte & 0xC0u) != 0x80u;
}

static uint32_t NextPoint(const muiAccessText* text, uint32_t at)
{
    if (at >= text->length)
    {
        return text->length;
    }
    at++;
    while (at < text->length && !IsLead(text->bytes[at]))
    {
        at++;
    }
    return at;
}

static uint32_t PreviousPoint(const muiAccessText* text, uint32_t at)
{
    at = at < text->length ? at : text->length;
    while (at > 0)
    {
        at--;
        if (IsLead(text->bytes[at]))
        {
            break;
        }
    }
    return at;
}

// The bytes of a mandatory break at a byte, 0 for none: LF, VT, FF, CR
// (with an LF after it), NEL, LS and PS.
static uint32_t BreakAt(const muiAccessText* text, uint32_t at)
{
    const unsigned char* bytes = (const unsigned char*)text->bytes;
    uint32_t left = text->length - at;
    if (bytes[at] == '\r')
    {
        return left > 1 && bytes[at + 1] == '\n' ? 2 : 1;
    }
    if (bytes[at] == '\n' || bytes[at] == '\v' || bytes[at] == '\f')
    {
        return 1;
    }
    if (left > 1 && bytes[at] == 0xC2 && bytes[at + 1] == 0x85)
    {
        return 2;
    }
    return left > 2 && bytes[at] == 0xE2 && bytes[at + 1] == 0x80 &&
                   (bytes[at + 2] == 0xA8 || bytes[at + 2] == 0xA9)
               ? 3
               : 0;
}

// A boundary of a unit given by starts (words' or lines'), from the
// marks: the nth start; whether there is one.
static bool StartOf(const muiAccessText* text, muiAccessUnit unit, uint32_t n, uint32_t* startOut)
{
    const muiAccessTextMarks* marks = text->marks;
    if (unit == mui_unitWord && n < marks->wordCount)
    {
        *startOut = marks->words[n].start;
        return true;
    }
    if (unit == mui_unitLine && n < marks->lineCount)
    {
        *startOut = marks->lineStarts[n];
        return true;
    }
    return false;
}

// The nearer to a byte, on one side of it, of a boundary found and a
// candidate.
static uint32_t Nearer(uint32_t found, uint32_t candidate, uint32_t at, bool forward)
{
    if (forward)
    {
        return candidate > at && candidate < found ? candidate : found;
    }
    return candidate < at && candidate > found ? candidate : found;
}

// The boundary after a byte (forward) or before it, by the starts of a
// unit, or by its paragraphs' breaks; the text's ends otherwise.
static uint32_t Scan(const muiAccessText* text, muiAccessUnit unit, uint32_t at, bool forward)
{
    uint32_t found = forward ? text->length : 0;
    uint32_t start = 0;
    for (uint32_t n = 0; StartOf(text, unit, n, &start); n++)
    {
        found = Nearer(found, start, at, forward);
    }
    for (uint32_t i = 0; unit == mui_unitParagraph && i < text->length;)
    {
        uint32_t size = BreakAt(text, i);
        found = size != 0 ? Nearer(found, i + size, at, forward) : found;
        i += size != 0 ? size : 1;
    }
    return found;
}

uint32_t muiAccessBoundaryAfter(const muiAccessText* text, muiAccessUnit unit, uint32_t at)
{
    if (at >= text->length)
    {
        return text->length;
    }
    return unit == mui_unitCharacter  ? NextPoint(text, at)
           : unit == mui_unitDocument ? text->length
                                      : Scan(text, unit, at, true);
}

uint32_t muiAccessBoundaryBefore(const muiAccessText* text, muiAccessUnit unit, uint32_t at)
{
    if (at == 0)
    {
        return 0;
    }
    return unit == mui_unitCharacter  ? PreviousPoint(text, at)
           : unit == mui_unitDocument ? 0
                                      : Scan(text, unit, at, false);
}

uint32_t muiAccessPointsBefore(const char* text, uint32_t offset)
{
    uint32_t count = 0;
    for (uint32_t i = 0; i < offset; i++)
    {
        count += IsLead(text[i]) ? 1u : 0u;
    }
    return count;
}

uint32_t muiAccessUtf16Before(const char* text, uint32_t offset)
{
    uint32_t count = 0;
    for (uint32_t i = 0; i < offset; i++)
    {
        unsigned char byte = (unsigned char)text[i];
        // A lead of four bytes is a code point past the BMP: two units.
        count += IsLead(text[i]) ? (byte >= 0xF0 ? 2u : 1u) : 0u;
    }
    return count;
}

// The byte where a count ends, the code points weighed by weigh.
static uint32_t ByteOfCount(const muiAccessText* text, uint32_t count, bool utf16)
{
    uint32_t at = 0;
    uint32_t seen = 0;
    while (at < text->length)
    {
        uint32_t weight = utf16 && (unsigned char)text->bytes[at] >= 0xF0 ? 2u : 1u;
        if (seen + weight > count)
        {
            break;
        }
        seen += weight;
        at = NextPoint(text, at);
    }
    return at;
}

uint32_t muiAccessByteOfPoints(const muiAccessText* text, uint32_t points)
{
    return ByteOfCount(text, points, false);
}

uint32_t muiAccessByteOfUtf16(const muiAccessText* text, uint32_t units)
{
    return ByteOfCount(text, units, true);
}
