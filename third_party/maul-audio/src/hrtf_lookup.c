// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Bilinear lookup on an HRTF set's rings: a binary search for the rings
// around an elevation, then the two azimuths around the direction's on
// each, every step bounded.

#include "hrtf_lookup.h"

#include <math.h>

// The ring at or below an elevation already within the rings' range,
// short of the last ring when there are two or more.
static uint32_t RingBelow(const maudHrtf* hrtf, float elevation)
{
    uint32_t low = 0;
    uint32_t high = hrtf->ringCount - 1;
    while (high - low > 1)
    {
        uint32_t middle = low + (high - low) / 2;
        if (hrtf->elevations[middle] <= elevation)
        {
            low = middle;
        }
        else
        {
            high = middle;
        }
    }
    return low;
}

// The two directions around an azimuth on a ring, and how far towards
// the second the azimuth lies.
static void AroundOnRing(const maudHrtf* hrtf, uint32_t ring, float azimuth, uint32_t* firstOut,
                         uint32_t* secondOut, float* fractionOut)
{
    uint32_t count = hrtf->azimuths[ring];
    float position = azimuth * (float)count / 360.0f;
    uint32_t index = (uint32_t)position;
    index = index >= count ? count - 1 : index;
    *fractionOut = position - (float)index;
    *firstOut = hrtf->firstDirection[ring] + index;
    *secondOut = hrtf->firstDirection[ring] + (index + 1 == count ? 0 : index + 1);
}

maudHrtfNeighbours maudHrtfNeighboursOf(const maudHrtf* hrtf, float azimuthDegrees,
                                        float elevationDegrees)
{
    float azimuth = fmodf(azimuthDegrees, 360.0f);
    azimuth = azimuth < 0.0f ? azimuth + 360.0f : azimuth;
    // fmodf of a value just below a multiple of 360 can round up to 360.
    azimuth = azimuth >= 360.0f ? 0.0f : azimuth;
    uint32_t last = hrtf->ringCount - 1;
    float lowest = hrtf->elevations[0];
    float highest = hrtf->elevations[last];
    float elevation = elevationDegrees < lowest    ? lowest
                      : elevationDegrees > highest ? highest
                                                   : elevationDegrees;
    uint32_t below = RingBelow(hrtf, elevation);
    uint32_t above = below == last ? below : below + 1;
    float up = above == below ? 0.0f
                              : (elevation - hrtf->elevations[below]) /
                                    (hrtf->elevations[above] - hrtf->elevations[below]);
    maudHrtfNeighbours neighbours;
    float along[2];
    AroundOnRing(hrtf, below, azimuth, &neighbours.direction[0], &neighbours.direction[1],
                 &along[0]);
    AroundOnRing(hrtf, above, azimuth, &neighbours.direction[2], &neighbours.direction[3],
                 &along[1]);
    neighbours.weight[0] = (1.0f - up) * (1.0f - along[0]);
    neighbours.weight[1] = (1.0f - up) * along[0];
    neighbours.weight[2] = up * (1.0f - along[1]);
    neighbours.weight[3] = up * along[1];
    return neighbours;
}

float maudHrtfBlend(const maudHrtf* hrtf, const maudHrtfNeighbours* neighbours, uint32_t ear,
                    float* response)
{
    uint32_t taps = hrtf->taps;
    for (uint32_t tap = 0; tap < taps; ++tap)
    {
        response[tap] = 0.0f;
    }
    float delay = 0.0f;
    for (int i = 0; i < 4; ++i)
    {
        float weight = neighbours->weight[i];
        size_t at = 2u * (size_t)neighbours->direction[i] + ear;
        const float* source = hrtf->responses + at * taps;
        for (uint32_t tap = 0; tap < taps; ++tap)
        {
            response[tap] += weight * source[tap];
        }
        delay += weight * hrtf->delays[at];
    }
    return delay;
}
