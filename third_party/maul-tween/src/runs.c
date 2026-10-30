// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// First-fit runs with coalescing. A free run's first
// entry holds its length in index1 and the next free run's start plus 1
// in generation.

#include "runs.h"

#include "invariant.h"

static uint32_t Length(const mtwnRuns* runs, uint32_t start)
{
    return runs->entries[start].target.index1;
}

static uint32_t Next(const mtwnRuns* runs, uint32_t start)
{
    return runs->entries[start].target.generation;
}

static void SetRun(mtwnRuns* runs, uint32_t start, uint32_t length, uint32_t next)
{
    runs->entries[start].target = (mtwnTargetId){length, next};
}

mtwnRuns mtwnMakeRuns(mtwnBound* entries, uint32_t capacity)
{
    mtwnRuns runs = {entries, capacity, 0};
    if (capacity > 0)
    {
        SetRun(&runs, 0, capacity, 0);
        runs.free = 1;
    }
    return runs;
}

bool mtwnTakeRun(mtwnRuns* runs, uint32_t count, uint32_t* startOut)
{
    uint32_t previous = 0;
    for (uint32_t at = runs->free; at != 0; at = Next(runs, at - 1))
    {
        uint32_t start = at - 1;
        uint32_t length = Length(runs, start);
        if (length < count)
        {
            previous = at;
            continue;
        }
        // The front of the run is taken; what is left stays in its place.
        uint32_t rest = length - count;
        uint32_t next = Next(runs, start);
        uint32_t replacement = next;
        if (rest > 0)
        {
            SetRun(runs, start + count, rest, next);
            replacement = start + count + 1;
        }
        if (previous == 0)
        {
            runs->free = replacement;
        }
        else
        {
            SetRun(runs, previous - 1, Length(runs, previous - 1), replacement);
        }
        MTWN_ASSERT(start + count <= runs->capacity);
        *startOut = start;
        return true;
    }
    return false;
}

void mtwnGiveRun(mtwnRuns* runs, uint32_t start, uint32_t count)
{
    // The free runs before and after it, by their starts.
    uint32_t before = 0;
    uint32_t after = runs->free;
    while (after != 0 && after - 1 < start)
    {
        before = after;
        after = Next(runs, after - 1);
    }
    // A run given back was taken: it lies inside the entries and overlaps
    // neither free run beside it.
    MTWN_ASSERT(count > 0 && start + count <= runs->capacity);
    MTWN_ASSERT(after == 0 || start + count <= after - 1);
    MTWN_ASSERT(before == 0 || before - 1 + Length(runs, before - 1) <= start);
    uint32_t length = count;
    uint32_t next = after;
    if (after != 0 && start + count == after - 1)
    {
        length += Length(runs, after - 1);
        next = Next(runs, after - 1);
    }
    if (before != 0 && before - 1 + Length(runs, before - 1) == start)
    {
        SetRun(runs, before - 1, Length(runs, before - 1) + length, next);
        return;
    }
    SetRun(runs, start, length, next);
    if (before == 0)
    {
        runs->free = start + 1;
    }
    else
    {
        SetRun(runs, before - 1, Length(runs, before - 1), start + 1);
    }
}
