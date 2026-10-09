// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Finding a direction in a loaded HRTF set: the four measured directions
// around it on the set's rings, weighted bilinearly, and an ear's
// response and delay blended from them.

#ifndef MAUL_AUDIO_SRC_HRTF_LOOKUP_H
#define MAUL_AUDIO_SRC_HRTF_LOOKUP_H

#include "hrtf_core.h"

#include <stdint.h>

// Four measured directions and their weights, which add up to 1: two
// azimuths on the ring at or below the elevation, two on the ring above.
typedef struct maudHrtfNeighbours
{
    uint32_t direction[4];
    float weight[4];
} maudHrtfNeighbours;

// The neighbours of a direction in the set's convention: azimuth in
// degrees counterclockwise from straight ahead (any value; it wraps),
// elevation in degrees up from the horizontal plane (clamped to the
// set's lowest and highest rings).
maudHrtfNeighbours maudHrtfNeighboursOf(const maudHrtf* hrtf, float azimuthDegrees,
                                        float elevationDegrees);

// Blends an ear's (0 left, 1 right) responses into response, the set's
// taps of it, and returns its delay in samples.
float maudHrtfBlend(const maudHrtf* hrtf, const maudHrtfNeighbours* neighbours, uint32_t ear,
                    float* response);

#endif // MAUL_AUDIO_SRC_HRTF_LOOKUP_H
