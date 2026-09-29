// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Rules L1 and L2 of UAX #9: the levels of one line and its visual order.
// L2 reverses, from the highest level down to the lowest odd one, every
// maximal stretch at or above that level; over runs, a line of n runs
// and levels up to 126 costs at most 126 n steps and no memory.

#include "bidi_line.h"

#include "bidi_core.h"

#include "maul-unicode/bidi.h"

#include <string.h>

// The runs being collected; count goes on past capacity.
typedef struct Runs
{
    muniBidiRun* runs;
    size_t capacity;
    size_t count;
    size_t end;    // where the last run ends
    uint8_t level; // the last run's level
} Runs;

static void Add(Runs* runs, size_t start, size_t end, uint8_t level)
{
    if (start == end)
    {
        return;
    }
    if (runs->count > 0 && runs->level == level && runs->end == start)
    {
        if (runs->count <= runs->capacity)
        {
            runs->runs[runs->count - 1].length += end - start;
        }
    }
    else
    {
        if (runs->count < runs->capacity)
        {
            runs->runs[runs->count] = (muniBidiRun){start, end - start, level};
        }
        runs->count += 1;
        runs->level = level;
    }
    runs->end = end;
}

// Bytes with their own levels.
static void AddOwn(Runs* runs, const uint8_t* levels, size_t start, size_t end)
{
    for (size_t index = start; index < end; index++)
    {
        Add(runs, index, index + 1, levels[index]);
    }
}

// What rule L1 resets before a separator or at the end of the line:
// whitespace and isolate formatting characters, and the characters rule
// X9 removed, which have no level of their own.
static bool IsTrailing(uint8_t type)
{
    switch (type)
    {
    case muni_bcWs:
    case muni_bcFsi:
    case muni_bcLri:
    case muni_bcRli:
    case muni_bcPdi:
    case muni_bcBn:
    case muni_bcLre:
    case muni_bcRle:
    case muni_bcLro:
    case muni_bcRlo:
    case muni_bcPdf:
        return true;
    default:
        return false;
    }
}

// L1: the runs of the line in logical order.
static void CollectRuns(Runs* runs, const uint8_t* text, const uint8_t* levels, size_t length,
                        uint8_t paragraphLevel)
{
    size_t trailing = SIZE_MAX; // where a stretch of IsTrailing code points starts
    size_t index = 0;
    while (index < length)
    {
        size_t size;
        uint8_t type = muniLookupBidiClass(muniBidiCodePoint(text, length, index, &size));
        size_t start = trailing != SIZE_MAX ? trailing : index;
        if (type == muni_bcS || type == muni_bcB)
        {
            Add(runs, start, index + size, paragraphLevel);
            trailing = SIZE_MAX;
        }
        else if (IsTrailing(type))
        {
            trailing = start;
        }
        else
        {
            AddOwn(runs, levels, start, index + size);
            trailing = SIZE_MAX;
        }
        index += size;
    }
    if (trailing != SIZE_MAX)
    {
        Add(runs, trailing, length, paragraphLevel);
    }
}

void muniBidiResetLineLevels(const uint8_t* line, uint8_t* levels, size_t length,
                             uint8_t paragraphLevel)
{
    size_t trailing = SIZE_MAX;
    size_t index = 0;
    while (index < length)
    {
        size_t size;
        uint8_t type = muniLookupBidiClass(muniBidiCodePoint(line, length, index, &size));
        size_t start = trailing != SIZE_MAX ? trailing : index;
        if (type == muni_bcS || type == muni_bcB)
        {
            memset(levels + start, paragraphLevel, index + size - start);
            trailing = SIZE_MAX;
        }
        else
        {
            trailing = IsTrailing(type) ? start : SIZE_MAX;
        }
        index += size;
    }
    if (trailing != SIZE_MAX)
    {
        memset(levels + trailing, paragraphLevel, length - trailing);
    }
}

static void ReverseRuns(muniBidiRun* runs, size_t first, size_t end)
{
    while (first + 1 < end)
    {
        end -= 1;
        muniBidiRun swap = runs[first];
        runs[first] = runs[end];
        runs[end] = swap;
        first += 1;
    }
}

// L2 over runs.
static void OrderRuns(muniBidiRun* runs, size_t count)
{
    uint8_t highest = 0;
    uint8_t lowest = UINT8_MAX;
    for (size_t k = 0; k < count; k++)
    {
        highest = runs[k].level > highest ? runs[k].level : highest;
        lowest = runs[k].level < lowest ? runs[k].level : lowest;
    }
    for (unsigned level = highest; level >= (unsigned)(lowest | 1) && level > 0; level--)
    {
        size_t k = 0;
        while (k < count)
        {
            if (runs[k].level < level)
            {
                k += 1;
                continue;
            }
            size_t end = k;
            while (end < count && runs[end].level >= level)
            {
                end += 1;
            }
            ReverseRuns(runs, k, end);
            k = end;
        }
    }
}

muniResult muniReorderBidiLine(const char* line, const uint8_t* levels, size_t length,
                               uint8_t paragraphLevel, muniBidiRun* runs, size_t capacity,
                               size_t* countOut)
{
    if (((line == nullptr || levels == nullptr) && length != 0) ||
        (runs == nullptr && capacity != 0) || countOut == nullptr)
    {
        return muni_errorInvalid;
    }
    Runs collected = {runs, capacity, 0, 0, 0};
    CollectRuns(&collected, (const uint8_t*)line, levels, length, paragraphLevel);
    *countOut = collected.count;
    if (collected.count > capacity)
    {
        return muni_errorCapacity;
    }
    OrderRuns(runs, collected.count);
    return muni_success;
}

static void ReverseIndexes(size_t* map, size_t first, size_t end)
{
    while (first + 1 < end)
    {
        end -= 1;
        size_t swap = map[first];
        map[first] = map[end];
        map[end] = swap;
        first += 1;
    }
}

muniResult muniReorderBidiLevels(const uint8_t* levels, size_t count, size_t* visualToLogicalOut)
{
    if ((levels == nullptr || visualToLogicalOut == nullptr) && count != 0)
    {
        return muni_errorInvalid;
    }
    uint8_t highest = 0;
    uint8_t lowest = UINT8_MAX;
    for (size_t k = 0; k < count; k++)
    {
        visualToLogicalOut[k] = k;
        highest = levels[k] > highest ? levels[k] : highest;
        lowest = levels[k] < lowest ? levels[k] : lowest;
    }
    size_t* map = visualToLogicalOut;
    for (unsigned level = highest; level >= (unsigned)(lowest | 1) && level > 0; level--)
    {
        size_t k = 0;
        while (k < count)
        {
            if (levels[map[k]] < level)
            {
                k += 1;
                continue;
            }
            size_t end = k;
            while (end < count && levels[map[end]] >= level)
            {
                end += 1;
            }
            ReverseIndexes(map, k, end);
            k = end;
        }
    }
    return muni_success;
}

muniResult muniInvertBidiMap(const size_t* map, size_t count, size_t* inverseOut)
{
    if ((map == nullptr || inverseOut == nullptr) && count != 0)
    {
        return muni_errorInvalid;
    }
    for (size_t k = 0; k < count; k++)
    {
        if (map[k] >= count)
        {
            return muni_errorInvalid;
        }
        inverseOut[map[k]] = k;
    }
    return muni_success;
}
