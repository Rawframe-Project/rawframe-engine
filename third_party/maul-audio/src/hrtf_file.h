// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The .maudhrtf file (docs/hrtf-format.md), read in place: every count,
// size, ring, text and the checksum checked, nothing allocated.

#ifndef MAUL_AUDIO_SRC_HRTF_FILE_H
#define MAUL_AUDIO_SRC_HRTF_FILE_H

#include "crc32.h"

#include "maul-audio/base.h"

#include <stddef.h>
#include <stdint.h>

// A well-formed file's fields, and where its parts lie in its bytes.
typedef struct maudHrtfFile
{
    uint32_t sampleRate;
    uint32_t taps;
    uint32_t ringCount;
    uint32_t directionCount;
    float scale;
    // Metres from the head's centre at which the set was measured.
    float distance;
    const uint8_t* name;
    uint32_t nameLength;
    const uint8_t* license;
    uint32_t licenseLength;
    // Eight bytes per ring: elevation (float32), azimuth count (uint32).
    const uint8_t* rings;
    // Two uint16 per direction, in 1/256 samples.
    const uint8_t* delays;
    // 2 x taps int16 per direction.
    const uint8_t* samples;
} maudHrtfFile;

// Checks count bytes as a .maudhrtf file and describes it in fileOut.
// maud_errorInvalid for anything malformed, maud_errorUnsupported for
// another format version.
maudResult maudReadHrtfFile(const void* bytes, size_t count, maudHrtfFile* fileOut);

// A ring's elevation in degrees and its azimuth count.
float maudHrtfFileElevation(const maudHrtfFile* file, uint32_t ring);
uint32_t maudHrtfFileAzimuths(const maudHrtfFile* file, uint32_t ring);

// A direction's delay for ear 0 (left) or 1 (right), in samples.
float maudHrtfFileDelay(const maudHrtfFile* file, uint32_t direction, uint32_t ear);

// A direction's response for an ear, as floats, taps of them.
void maudHrtfFileResponse(const maudHrtfFile* file, uint32_t direction, uint32_t ear, float* out);

#endif // MAUL_AUDIO_SRC_HRTF_FILE_H
