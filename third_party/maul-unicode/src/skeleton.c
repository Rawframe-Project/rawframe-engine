// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Confusable skeletons (UTS #39 section 4). The code points come in
// display order, as rules L1 to L4 of UAX #9 arrange them: odd levels
// reversed, combining marks kept after their base (L3), and mirrored
// characters replaced by their mirror (L4). They then pass two stages
// of canonical ordering: the first gives NFD of the text, whose code
// points, but the default ignorables, are replaced by their prototypes;
// the second gives NFD of those, which is the skeleton. A stage holds a
// starter and the marks after it, as normalization does, and passes
// them on when the next starter comes.
//
// Text without right-to-left characters, embeddings or overrides has
// only even levels in a paragraph that is not right to left, so its
// display order is its logical order, and bidi is skipped.

#include "bidi_line.h"
#include "bits.h"
#include "decompose.h"
#include "encoding.h"
#include "tables.h"
#include "writer.h"

#include "maul-unicode/security.h"

#define MAX_SEGMENT 32
#define POOLED      0x8000u

typedef struct Stage
{
    uint32_t codePoints[MAX_SEGMENT];
    uint8_t classes[MAX_SEGMENT];
    size_t count;
} Stage;

typedef struct Skeleton
{
    const uint8_t* text;
    const uint8_t* levels; // the levels after L1, or NULL for logical order
    uint8_t lowestOdd;     // the lowest odd level of the paragraph, which L2 reverses
    Stage decomposed;      // NFD of the text
    Stage mapped;          // NFD of the prototypes
    size_t offset;         // where the code point being added starts
    size_t overflow;       // where a stage first outgrew MAX_SEGMENT, or SIZE_MAX
    muniWriter writer;
} Skeleton;

// Canonical ordering: a mark goes after every mark of the same or a
// lower class. The caller passes a starter on to an empty stage.
static void Place(Skeleton* skeleton, Stage* stage, uint32_t codePoint, uint8_t markClass)
{
    if (stage->count == MAX_SEGMENT)
    {
        skeleton->overflow =
            skeleton->overflow < skeleton->offset ? skeleton->overflow : skeleton->offset;
        return;
    }
    size_t at = stage->count;
    for (; at > 0 && stage->classes[at - 1] > markClass; at--)
    {
        stage->codePoints[at] = stage->codePoints[at - 1];
        stage->classes[at] = stage->classes[at - 1];
    }
    stage->codePoints[at] = codePoint;
    stage->classes[at] = markClass;
    stage->count += 1;
}

static uint8_t ClassOf(uint32_t codePoint)
{
    return codePoint < 0x300 ? 0 : muniLookupCombiningClass(codePoint);
}

static void FlushMapped(Skeleton* skeleton)
{
    for (size_t i = 0; i < skeleton->mapped.count; i++)
    {
        muniWriterPut(&skeleton->writer, skeleton->mapped.codePoints[i]);
    }
    skeleton->mapped.count = 0;
}

// Adds a code point of a prototype to the second stage.
static void AddMapped(Skeleton* skeleton, uint32_t codePoint)
{
    uint32_t parts[MUNI_MAX_DECOMPOSITION];
    size_t count = 1;
    parts[0] = codePoint;
    if (codePoint >= 0xA0) // nothing below U+00A0 decomposes
    {
        count = muniDecomposeFully(codePoint, false, parts);
    }
    for (size_t i = 0; i < count; i++)
    {
        uint8_t markClass = ClassOf(parts[i]);
        if (markClass == 0)
        {
            FlushMapped(skeleton);
        }
        Place(skeleton, &skeleton->mapped, parts[i], markClass);
    }
}

// The position of a code point's entry among the confusables, or -1.
static int32_t ConfusableIndex(uint32_t codePoint)
{
    uint32_t block = codePoint >> 6;
    uint32_t low = 0;
    uint32_t high = muniConfusableBlockCount;
    if (block < high && muniConfusableStarts[block] == block)
    {
        // The starts rise by at least 1 each, so every block up to this one
        // is present, as the first blocks of the BMP are.
        low = high = block;
    }
    while (low < high)
    {
        uint32_t middle = (low + high) / 2;
        if (muniConfusableStarts[middle] < block)
        {
            low = middle + 1;
        }
        else
        {
            high = middle;
        }
    }
    if (low == muniConfusableBlockCount || muniConfusableStarts[low] != block)
    {
        return -1;
    }
    return muniRank(low + 1, codePoint, muniConfusableBits, muniConfusableRanks);
}

// Passes the prototype of a code point to the second stage.
static void AddPrototype(Skeleton* skeleton, uint32_t codePoint)
{
    int32_t index = ConfusableIndex(codePoint);
    uint32_t entry = index < 0 ? codePoint : muniConfusables[index];
    if (index < 0 || entry < POOLED)
    {
        AddMapped(skeleton, entry);
        return;
    }
    uint32_t offset = entry & 0x1FFF;
    uint32_t length = entry >> 13 & 3;
    if (length == 0)
    {
        length = muniConfusablePool[offset++];
    }
    for (; length > 0; length--)
    {
        uint32_t unit = muniConfusablePool[offset++];
        if (unit - 0xD800 < 0x400)
        {
            unit = 0x10000 + ((unit - 0xD800) << 10) + (muniConfusablePool[offset++] - 0xDC00);
        }
        AddMapped(skeleton, unit);
    }
}

static void FlushDecomposed(Skeleton* skeleton)
{
    for (size_t i = 0; i < skeleton->decomposed.count; i++)
    {
        uint32_t codePoint = skeleton->decomposed.codePoints[i];
        if (codePoint < 0x80 || muniLookupDefaultIgnorable(codePoint) == 0)
        {
            AddPrototype(skeleton, codePoint);
        }
    }
    skeleton->decomposed.count = 0;
}

// Adds a code point of the text, in display order, to the first stage.
static void AddDecomposed(Skeleton* skeleton, uint32_t codePoint)
{
    uint32_t parts[MUNI_MAX_DECOMPOSITION];
    size_t count = 1;
    parts[0] = codePoint;
    if (codePoint >= 0xA0) // nothing below U+00A0 decomposes
    {
        count = muniDecomposeFully(codePoint, false, parts);
    }
    for (size_t i = 0; i < count; i++)
    {
        uint8_t markClass = ClassOf(parts[i]);
        if (markClass == 0)
        {
            FlushDecomposed(skeleton);
        }
        Place(skeleton, &skeleton->decomposed, parts[i], markClass);
    }
}

static uint32_t Decode(const Skeleton* skeleton, size_t offset, size_t end, size_t* sizeOut)
{
    uint32_t codePoint;
    (void)muniStepUtf8(skeleton->text + offset, end - offset, &codePoint, sizeOut);
    return codePoint;
}

// Adds text[start, end) in logical order; code points at an odd level
// display mirrored (L4).
static void AddForward(Skeleton* skeleton, size_t start, size_t end)
{
    while (start < end)
    {
        size_t size;
        uint32_t codePoint = Decode(skeleton, start, end, &size);
        bool odd = skeleton->levels != nullptr && (skeleton->levels[start] & 1) != 0;
        skeleton->offset = start;
        AddDecomposed(skeleton, odd ? muniGetMirroringGlyph(codePoint) : codePoint);
        start += size;
    }
}

static bool IsMark(uint32_t codePoint)
{
    uint8_t category = muniLookupGeneralCategory(codePoint);
    return category == muni_gcMn || category == muni_gcMc || category == muni_gcMe;
}

// Adds text[start, end) reversed, but each base with the combining marks
// after it kept in logical order (L3).
static void AddBackward(Skeleton* skeleton, size_t start, size_t end)
{
    while (end > start)
    {
        size_t at = end;
        bool mark;
        do
        {
            at -= 1;
            while (at > start && (skeleton->text[at] & 0xC0) == 0x80)
            {
                at -= 1;
            }
            size_t size;
            mark = IsMark(Decode(skeleton, at, end, &size));
        } while (mark && at > start);
        AddForward(skeleton, at, end);
        end = at;
    }
}

// Adds text[start, end), whose levels are all at least level, in display
// order (L2): reversed once for each level from level down to the lowest
// odd one that reverses it, which backward carries from the levels below.
// Stretches at higher levels recurse, one level deeper each time.
static void AddStretch(Skeleton* skeleton, size_t start, size_t end, uint8_t level, bool backward)
{
    const uint8_t* levels = skeleton->levels;
    bool reversed = backward != (level >= skeleton->lowestOdd);
    size_t index = reversed ? end : start;
    while (reversed ? index > start : index < end)
    {
        size_t from = index;
        bool higher = levels[reversed ? index - 1 : index] > level;
        if (reversed)
        {
            for (; index > start && (levels[index - 1] > level) == higher; index--)
            {
            }
        }
        else
        {
            for (; index < end && (levels[index] > level) == higher; index++)
            {
            }
        }
        size_t first = reversed ? index : from;
        size_t last = reversed ? from : index;
        if (higher)
        {
            AddStretch(skeleton, first, last, (uint8_t)(level + 1), reversed);
        }
        else if (reversed)
        {
            AddBackward(skeleton, first, last);
        }
        else
        {
            AddForward(skeleton, first, last);
        }
    }
}

// Whether the text needs bidi: a right-to-left paragraph, or a code
// point that can open an odd level.
static bool NeedsBidi(const uint8_t* text, size_t length, muniBidiDirection direction)
{
    if (direction == muni_bidiRightToLeft)
    {
        return true;
    }
    size_t offset = 0;
    while (offset < length)
    {
        if (text[offset] < 0x80)
        {
            offset += 1;
            continue;
        }
        uint32_t codePoint;
        size_t size;
        (void)muniStepUtf8(text + offset, length - offset, &codePoint, &size);
        switch (muniLookupBidiClass(codePoint))
        {
        case muni_bcR:
        case muni_bcAl:
        case muni_bcRle:
        case muni_bcRlo:
        case muni_bcRli:
            return true;
        default:
            offset += size;
        }
    }
    return false;
}

// Adds each paragraph in display order.
static muniResult AddParagraphs(Skeleton* skeleton, size_t length, muniBidiDirection direction,
                                uint8_t* workspace)
{
    uint8_t* levels = workspace;
    skeleton->levels = levels;
    size_t start = 0;
    while (start < length)
    {
        size_t paragraphLength;
        uint8_t paragraphLevel;
        muniResult status = muniResolveBidi((const char*)skeleton->text + start, length - start,
                                            direction, levels + start, workspace + length + start,
                                            &paragraphLength, &paragraphLevel);
        if (status != muni_success)
        {
            return status;
        }
        muniBidiResetLineLevels(skeleton->text + start, levels + start, paragraphLength,
                                paragraphLevel);
        uint8_t lowest = UINT8_MAX;
        for (size_t i = start; i < start + paragraphLength; i++)
        {
            lowest = levels[i] < lowest ? levels[i] : lowest;
        }
        skeleton->lowestOdd = lowest | 1;
        AddStretch(skeleton, start, start + paragraphLength, lowest, false);
        start += paragraphLength;
    }
    return muni_success;
}

muniTextResult muniGetSkeleton(const char* text, size_t length, muniBidiDirection direction,
                               uint8_t* workspace, char* output, size_t capacity, size_t* neededOut)
{
    if ((text == nullptr && length != 0) || (workspace == nullptr && length != 0) ||
        (output == nullptr && capacity != 0) || neededOut == nullptr ||
        direction > muni_bidiRightToLeft)
    {
        return (muniTextResult){muni_errorInvalid, 0};
    }
    muniTextResult valid = muniValidateUtf8(text, length);
    if (valid.status != muni_success)
    {
        *neededOut = 0;
        return valid;
    }
    Skeleton skeleton = {
        .text = (const uint8_t*)text, .overflow = SIZE_MAX, .writer = {output, capacity, 0}};
    if (NeedsBidi(skeleton.text, length, direction))
    {
        muniResult status = AddParagraphs(&skeleton, length, direction, workspace);
        if (status != muni_success)
        {
            return (muniTextResult){status, 0};
        }
    }
    else
    {
        AddForward(&skeleton, 0, length);
    }
    FlushDecomposed(&skeleton);
    FlushMapped(&skeleton);
    *neededOut = skeleton.writer.needed;
    if (skeleton.overflow != SIZE_MAX)
    {
        return (muniTextResult){muni_errorLimit, skeleton.overflow};
    }
    return (muniTextResult){skeleton.writer.needed > capacity ? muni_errorCapacity : muni_success,
                            length};
}
