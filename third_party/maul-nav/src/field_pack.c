// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Packing open-space fields (mnav-0016). The planes, in order: the
// columns' span counts (low bytes, then high), the spans' floors and
// heights likewise, then their areas; 2 bytes a column and 5 a span
// before coding.

#include "field_pack.h"

#include "allocator.h"
#include "compact.h"
#include "lz.h"

#include "maul-nav/base.h"

#include <stdalign.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

static size_t Columns(const mnavTileFrame* frame)
{
    return (size_t)frame->width * (size_t)frame->width;
}

static size_t PlaneBytes(const mnavTileFrame* frame, int32_t spanCount)
{
    return 2 * Columns(frame) + 5 * (size_t)spanCount;
}

// Writes a 16-bit value at i of a plane pair of count values: its low
// byte, and its high byte count bytes on.
static void Put(uint8_t* plane, size_t count, size_t i, uint16_t value)
{
    plane[i] = (uint8_t)value;
    plane[count + i] = (uint8_t)(value >> 8);
}

static uint16_t Get(const uint8_t* plane, size_t count, size_t i)
{
    return (uint16_t)(plane[i] | plane[count + i] << 8);
}

static void Flatten(const mnavCompactField* field, uint8_t* raw)
{
    size_t columns = Columns(&field->frame);
    size_t spans = (size_t)field->spanCount;
    for (size_t c = 0; c < columns; ++c)
    {
        // Fewer than 32,768 spans fit in one column.
        Put(raw, columns, c, (uint16_t)(field->columns[c + 1] - field->columns[c]));
    }
    uint8_t* floors = raw + 2 * columns;
    uint8_t* heights = floors + 2 * spans;
    uint8_t* areas = heights + 2 * spans;
    for (size_t i = 0; i < spans; ++i)
    {
        Put(floors, spans, i, field->spans[i].floor);
        Put(heights, spans, i, field->spans[i].height);
        areas[i] = field->areas[i];
    }
}

mnavResult mnavPackField(mnavMemory* scratch, mnavMemory* memory, const mnavCompactField* field,
                         mnavPackedField* packed)
{
    *packed = (mnavPackedField){.frame = field->frame, .spanCount = field->spanCount};
    size_t rawSize = PlaneBytes(&field->frame, field->spanCount);
    size_t bound = mnavLzBound(rawSize);
    uint8_t* raw = nullptr;
    uint8_t* coded = nullptr;
    uint32_t* table = nullptr;
    mnavResult result = mnavAllocate(scratch, rawSize, 1, 1, (void**)&raw);
    if (result == mnav_success)
    {
        result = mnavAllocate(scratch, bound, 1, 1, (void**)&coded);
    }
    if (result == mnav_success)
    {
        result = mnavAllocate(scratch, MNAV_LZ_TABLE, sizeof(uint32_t), alignof(uint32_t),
                              (void**)&table);
    }
    if (result == mnav_success)
    {
        Flatten(field, raw);
        size_t size = mnavLzEncode(raw, rawSize, coded, table);
        result = mnavAllocate(memory, size, 1, 1, (void**)&packed->bytes);
        if (result == mnav_success)
        {
            memcpy(packed->bytes, coded, size);
            packed->size = size;
        }
    }
    mnavRelease(scratch, table, MNAV_LZ_TABLE, sizeof(uint32_t), alignof(uint32_t));
    mnavRelease(scratch, coded, bound, 1, 1);
    mnavRelease(scratch, raw, rawSize, 1, 1);
    return result;
}

// Fills a field from its planes; false when its counts do not add up to
// its spans.
static bool Fill(const uint8_t* raw, mnavCompactField* field)
{
    size_t columns = Columns(&field->frame);
    size_t spans = (size_t)field->spanCount;
    uint32_t written = 0;
    for (size_t c = 0; c < columns; ++c)
    {
        field->columns[c] = written;
        written += Get(raw, columns, c);
    }
    field->columns[columns] = written;
    if (written != spans)
    {
        return false;
    }
    const uint8_t* floors = raw + 2 * columns;
    const uint8_t* heights = floors + 2 * spans;
    const uint8_t* areas = heights + 2 * spans;
    for (size_t i = 0; i < spans; ++i)
    {
        field->spans[i] = (mnavOpenSpan){
            Get(floors, spans, i),
            Get(heights, spans, i),
            {MNAV_NO_LINK, MNAV_NO_LINK, MNAV_NO_LINK, MNAV_NO_LINK},
        };
        field->areas[i] = areas[i];
    }
    return true;
}

mnavResult mnavUnpackField(mnavMemory* memory, const mnavPackedField* packed, int32_t height,
                           int32_t step, mnavCompactField* field)
{
    *field = (mnavCompactField){0};
    size_t rawSize = PlaneBytes(&packed->frame, packed->spanCount);
    uint8_t* raw = nullptr;
    mnavResult result = mnavAllocate(memory, rawSize, 1, 1, (void**)&raw);
    if (result == mnav_success && !mnavLzDecode(packed->bytes, packed->size, raw, rawSize))
    {
        result = mnav_errorInvalid;
    }
    if (result == mnav_success)
    {
        result = mnavAllocateCompactField(memory, &packed->frame, packed->spanCount, field);
    }
    if (result == mnav_success && !Fill(raw, field))
    {
        mnavReleaseCompactField(memory, field);
        result = mnav_errorInvalid;
    }
    mnavRelease(memory, raw, rawSize, 1, 1);
    if (result == mnav_success)
    {
        mnavLinkCompactField(field, height, step);
    }
    return result;
}

void mnavReleasePackedField(mnavMemory* memory, mnavPackedField* packed)
{
    mnavRelease(memory, packed->bytes, packed->size, 1, 1);
    *packed = (mnavPackedField){0};
}
