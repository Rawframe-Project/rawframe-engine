// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The open-space field.

#include "compact.h"

#include "allocator.h"
#include "heightfield.h"

#include "maul-nav/bake.h"

#include <stdint.h>

// The top of the open space above a column's highest span.
#define OPEN_TOP 65535

static int32_t CountWalkable(const mnavHeightfield* heightfield)
{
    int32_t count = 0;
    for (int32_t i = 0; i < heightfield->spanCount; ++i)
    {
        count += heightfield->spans[i].area != mnav_areaNone;
    }
    return count;
}

static void FillSpans(const mnavHeightfield* heightfield, mnavCompactField* field)
{
    int32_t columnCount = field->frame.width * field->frame.width;
    uint32_t written = 0;
    for (int32_t c = 0; c < columnCount; ++c)
    {
        field->columns[c] = written;
        uint32_t end = heightfield->columns[c + 1];
        for (uint32_t i = heightfield->columns[c]; i < end; ++i)
        {
            const mnavSpan* span = &heightfield->spans[i];
            if (span->area == mnav_areaNone)
            {
                continue;
            }
            int32_t ceiling = i + 1 < end ? heightfield->spans[i + 1].bottom : OPEN_TOP;
            field->spans[written] = (mnavOpenSpan){
                span->top,
                (uint16_t)(ceiling - span->top),
                {MNAV_NO_LINK, MNAV_NO_LINK, MNAV_NO_LINK, MNAV_NO_LINK},
            };
            field->areas[written] = span->area;
            ++written;
        }
    }
    field->columns[columnCount] = written;
}

static int32_t Min(int32_t a, int32_t b)
{
    return a < b ? a : b;
}

static int32_t Max(int32_t a, int32_t b)
{
    return a > b ? a : b;
}

// The layer index of the lowest span in a column that span shares at
// least height cells of space with and whose floor is within step, or
// MNAV_NO_LINK.
static uint16_t FindLink(const mnavCompactField* field, const mnavOpenSpan* span, int32_t column,
                         int32_t height, int32_t step)
{
    uint32_t first = field->columns[column];
    uint32_t end = field->columns[column + 1];
    int32_t top = span->floor + span->height;
    for (uint32_t k = first; k < end; ++k)
    {
        const mnavOpenSpan* other = &field->spans[k];
        int32_t shared = Min(top, other->floor + other->height) - Max(span->floor, other->floor);
        int32_t rise = (int32_t)other->floor - (int32_t)span->floor;
        if (shared >= height && rise >= -step && rise <= step)
        {
            // Fewer than 32,768 walkable spans fit in one column.
            return (uint16_t)(k - first);
        }
    }
    return MNAV_NO_LINK;
}

static void Link(mnavCompactField* field, int32_t height, int32_t step)
{
    int32_t width = field->frame.width;
    for (int32_t z = 0; z < width; ++z)
    {
        for (int32_t x = 0; x < width; ++x)
        {
            uint32_t end = field->columns[x + z * width + 1];
            for (uint32_t i = field->columns[x + z * width]; i < end; ++i)
            {
                for (int32_t direction = 0; direction < 4; ++direction)
                {
                    int32_t nx = x + mnavDirectionX(direction);
                    int32_t nz = z + mnavDirectionZ(direction);
                    if (nx < 0 || nz < 0 || nx >= width || nz >= width)
                    {
                        continue;
                    }
                    field->spans[i].links[direction] =
                        FindLink(field, &field->spans[i], nx + nz * width, height, step);
                }
            }
        }
    }
}

mnavResult mnavBuildCompactField(mnavMemory* memory, const mnavHeightfield* heightfield,
                                 int32_t height, int32_t step, mnavCompactField* field)
{
    *field = (mnavCompactField){0};
    field->frame = heightfield->frame;
    int32_t count = CountWalkable(heightfield);
    field->spanCount = count;
    size_t columnCount = (size_t)field->frame.width * (size_t)field->frame.width + 1;
    mnavResult result = mnavAllocate(memory, columnCount, sizeof(uint32_t), alignof(uint32_t),
                                     (void**)&field->columns);
    if (result == mnav_success)
    {
        result = mnavAllocate(memory, (size_t)count, sizeof(mnavOpenSpan), alignof(mnavOpenSpan),
                              (void**)&field->spans);
    }
    if (result == mnav_success)
    {
        result = mnavAllocate(memory, (size_t)count, sizeof(mnavAreaType), alignof(mnavAreaType),
                              (void**)&field->areas);
    }
    if (result != mnav_success)
    {
        mnavReleaseCompactField(memory, field);
        return result;
    }
    FillSpans(heightfield, field);
    Link(field, height, step);
    return mnav_success;
}

void mnavReleaseCompactField(mnavMemory* memory, mnavCompactField* field)
{
    if (field->columns != nullptr)
    {
        size_t columnCount = (size_t)field->frame.width * (size_t)field->frame.width + 1;
        mnavRelease(memory, field->columns, columnCount, sizeof(uint32_t), alignof(uint32_t));
    }
    mnavRelease(memory, field->spans, (size_t)field->spanCount, sizeof(mnavOpenSpan),
                alignof(mnavOpenSpan));
    mnavRelease(memory, field->areas, (size_t)field->spanCount, sizeof(mnavAreaType),
                alignof(mnavAreaType));
    *field = (mnavCompactField){0};
}
