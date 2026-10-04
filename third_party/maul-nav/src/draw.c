// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Appending to debug buffers (mnav-0010).

#include "draw.h"

#include "maul-nav/base.h"
#include "maul-nav/draw.h"

#include <math.h>
#include <stdbool.h>
#include <stdint.h>

// The most a list may hold: indices stay within 32 unsigned bits.
#define MOST INT32_MAX

static bool GoodList(const void* items, int32_t capacity, int32_t count)
{
    return capacity >= 0 && count >= 0 && (capacity == 0 || items != nullptr);
}

bool mnavGoodBuffer(const mnavDebugBuffer* buffer)
{
    return buffer != nullptr && isfinite(buffer->origin.x) && isfinite(buffer->origin.y) &&
           isfinite(buffer->origin.z) &&
           GoodList(buffer->vertices, buffer->vertexCapacity, buffer->vertexCount) &&
           GoodList(buffer->triangles, buffer->triangleCapacity, buffer->triangleCount) &&
           GoodList(buffer->lines, buffer->lineCapacity, buffer->lineCount);
}

uint32_t mnavDrawVertex(mnavDebugBuffer* buffer, mnavPos3 p, mnavDebugKind kind, uint16_t value)
{
    int32_t at = buffer->vertexCount;
    if (at < buffer->vertexCapacity)
    {
        buffer->vertices[at] =
            (mnavDebugVertex){(float)(p.x - buffer->origin.x), (float)(p.y - buffer->origin.y),
                              (float)(p.z - buffer->origin.z), kind, value};
    }
    buffer->vertexCount = at < MOST ? at + 1 : at;
    return (uint32_t)at;
}

// Appends an index to a list.
static void Index(uint32_t* list, int32_t capacity, int32_t* count, uint32_t index)
{
    if (*count < capacity)
    {
        list[*count] = index;
    }
    *count = *count < MOST ? *count + 1 : *count;
}

void mnavDrawLine(mnavDebugBuffer* buffer, mnavPos3 a, mnavPos3 b, mnavDebugKind kind,
                  uint16_t value)
{
    uint32_t ia = mnavDrawVertex(buffer, a, kind, value);
    uint32_t ib = mnavDrawVertex(buffer, b, kind, value);
    Index(buffer->lines, buffer->lineCapacity, &buffer->lineCount, ia);
    Index(buffer->lines, buffer->lineCapacity, &buffer->lineCount, ib);
}

void mnavDrawTriangle(mnavDebugBuffer* buffer, mnavPos3 a, mnavPos3 b, mnavPos3 c,
                      mnavDebugKind kind, uint16_t value)
{
    uint32_t ia = mnavDrawVertex(buffer, a, kind, value);
    uint32_t ib = mnavDrawVertex(buffer, b, kind, value);
    uint32_t ic = mnavDrawVertex(buffer, c, kind, value);
    Index(buffer->triangles, buffer->triangleCapacity, &buffer->triangleCount, ia);
    Index(buffer->triangles, buffer->triangleCapacity, &buffer->triangleCount, ib);
    Index(buffer->triangles, buffer->triangleCapacity, &buffer->triangleCount, ic);
}

mnavResult mnavDrawResult(const mnavDebugBuffer* buffer)
{
    bool fits = buffer->vertexCount <= buffer->vertexCapacity &&
                buffer->triangleCount <= buffer->triangleCapacity &&
                buffer->lineCount <= buffer->lineCapacity;
    return fits ? mnav_success : mnav_errorCapacity;
}
