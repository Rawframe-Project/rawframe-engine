// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// A loaded HRTF set, as the renderer reads it: one block from the def's
// allocator holding the rings, the delays and the responses as floats at
// the set's rate, and its texts.

#ifndef MAUL_AUDIO_SRC_HRTF_CORE_H
#define MAUL_AUDIO_SRC_HRTF_CORE_H

#include "maul-audio/hrtf.h"

struct maudHrtf
{
    maudAllocator allocator;
    size_t bytes;
    uint32_t sampleRate;
    uint32_t taps;
    uint32_t ringCount;
    uint32_t directionCount;
    // Metres from the head's centre at which the set was measured.
    float distance;
    // Per ring: its elevation in degrees, its azimuth count, and the index
    // of its first direction.
    float* elevations;
    uint32_t* azimuths;
    uint32_t* firstDirection;
    // Per direction, left then right: the onset delay in samples, and the
    // minimum-phase response of taps.
    float* delays;
    float* responses;
    char* name;
    size_t nameLength;
    char* license;
    size_t licenseLength;
};

#endif // MAUL_AUDIO_SRC_HRTF_CORE_H
