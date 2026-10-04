// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Erosion by the agent's radius.

#include "erode.h"

#include "allocator.h"
#include "compact.h"

#include "maul-nav/bake.h"

#include <stdint.h>

// Chamfer weights: a straight step and a diagonal one, in half cells, so
// a distance of 2 * radius is radius cells.
#define STRAIGHT 2
#define DIAGONAL 3
#define FAR      0xFFFF

static bool IsBoundary(const mnavCompactField* field, int32_t x, int32_t z, uint32_t i)
{
    if (field->areas[i] == mnav_areaNone)
    {
        return true;
    }
    for (int32_t direction = 0; direction < 4; ++direction)
    {
        if (field->spans[i].links[direction] == MNAV_NO_LINK ||
            field->areas[mnavLinkedSpan(field, x, z, i, direction)] == mnav_areaNone)
        {
            return true;
        }
    }
    return false;
}

static void Relax(uint16_t* distance, uint32_t i, uint32_t from, int32_t weight)
{
    int32_t candidate = distance[from] + weight;
    candidate = candidate > FAR ? FAR : candidate;
    if (candidate < distance[i])
    {
        distance[i] = (uint16_t)candidate;
    }
}

// Relaxes span i from its neighbor in direction first and from that
// neighbor's neighbor in direction second (the diagonal).
static void RelaxPair(const mnavCompactField* field, uint16_t* distance, int32_t x, int32_t z,
                      uint32_t i, int32_t first, int32_t second)
{
    if (field->spans[i].links[first] == MNAV_NO_LINK)
    {
        return;
    }
    uint32_t a = mnavLinkedSpan(field, x, z, i, first);
    Relax(distance, i, a, STRAIGHT);
    int32_t ax = x + mnavDirectionX(first);
    int32_t az = z + mnavDirectionZ(first);
    if (field->spans[a].links[second] != MNAV_NO_LINK)
    {
        Relax(distance, i, mnavLinkedSpan(field, ax, az, a, second), DIAGONAL);
    }
}

static void Sweep(const mnavCompactField* field, uint16_t* distance, bool forward)
{
    int32_t width = field->frame.width;
    for (int32_t step = 0; step < width * width; ++step)
    {
        int32_t c = forward ? step : width * width - 1 - step;
        int32_t x = c % width;
        int32_t z = c / width;
        for (uint32_t i = field->columns[c]; i < field->columns[c + 1]; ++i)
        {
            if (forward)
            {
                // From -X and from -Z, with the diagonals -X-Z and +X-Z.
                RelaxPair(field, distance, x, z, i, 0, 3);
                RelaxPair(field, distance, x, z, i, 3, 2);
            }
            else
            {
                // From +X and from +Z, with the diagonals +X+Z and -X+Z.
                RelaxPair(field, distance, x, z, i, 2, 1);
                RelaxPair(field, distance, x, z, i, 1, 0);
            }
        }
    }
}

mnavResult mnavErode(mnavMemory* memory, mnavCompactField* field, int32_t radius)
{
    uint16_t* distance = nullptr;
    mnavResult result = mnavAllocate(memory, (size_t)field->spanCount, sizeof(uint16_t),
                                     alignof(uint16_t), (void**)&distance);
    if (result != mnav_success)
    {
        return result;
    }
    int32_t width = field->frame.width;
    for (int32_t c = 0; c < width * width; ++c)
    {
        for (uint32_t i = field->columns[c]; i < field->columns[c + 1]; ++i)
        {
            distance[i] = IsBoundary(field, c % width, c / width, i) ? 0 : FAR;
        }
    }
    Sweep(field, distance, true);
    Sweep(field, distance, false);
    int32_t threshold = radius * STRAIGHT;
    for (int32_t i = 0; i < field->spanCount; ++i)
    {
        if (distance[i] < threshold)
        {
            field->areas[i] = mnav_areaNone;
        }
    }
    mnavRelease(memory, distance, (size_t)field->spanCount, sizeof(uint16_t), alignof(uint16_t));
    return mnav_success;
}
