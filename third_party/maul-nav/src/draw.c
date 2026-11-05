// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Appending to debug buffers.

#include "draw.h"

#include "maul-nav/base.h"
#include "maul-nav/draw.h"

#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// The most a list may hold: indices stay within 32 unsigned bits.
#define MOST INT32_MAX

MNAV_NODISCARD static bool GoodList(const void* items, int32_t capacity, int32_t count)
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

bool mnavGoodPoints(const mnavPos3* points, int32_t count)
{
    for (int32_t i = 0; i < count; ++i)
    {
        if (!isfinite(points[i].x) || !isfinite(points[i].y) || !isfinite(points[i].z))
        {
            return false;
        }
    }
    return true;
}

uint32_t mnavDrawVertex(mnavDebugBuffer* buffer, mnavPos3 p, mnavDebugKind kind, uint32_t value)
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

// Appends an item of n indices to a list counted in items.
static void Append(uint32_t* list, int32_t capacity, int32_t* count, const uint32_t* indices,
                   int32_t n)
{
    if (*count < capacity)
    {
        for (int32_t k = 0; k < n; ++k)
        {
            list[(size_t)*count * (size_t)n + (size_t)k] = indices[k];
        }
    }
    *count = *count < MOST ? *count + 1 : *count;
}

void mnavDrawLine(mnavDebugBuffer* buffer, mnavPos3 a, mnavPos3 b, mnavDebugKind kind,
                  uint32_t value)
{
    uint32_t ends[2] = {mnavDrawVertex(buffer, a, kind, value),
                        mnavDrawVertex(buffer, b, kind, value)};
    Append(buffer->lines, buffer->lineCapacity, &buffer->lineCount, ends, 2);
}

void mnavDrawTriangle(mnavDebugBuffer* buffer, mnavPos3 a, mnavPos3 b, mnavPos3 c,
                      mnavDebugKind kind, uint32_t value)
{
    uint32_t corners[3] = {mnavDrawVertex(buffer, a, kind, value),
                           mnavDrawVertex(buffer, b, kind, value),
                           mnavDrawVertex(buffer, c, kind, value)};
    Append(buffer->triangles, buffer->triangleCapacity, &buffer->triangleCount, corners, 3);
}

mnavResult mnavDrawResult(const mnavDebugBuffer* buffer)
{
    bool fits = buffer->vertexCount <= buffer->vertexCapacity &&
                buffer->triangleCount <= buffer->triangleCapacity &&
                buffer->lineCount <= buffer->lineCapacity;
    return fits ? mnav_success : mnav_errorCapacity;
}
