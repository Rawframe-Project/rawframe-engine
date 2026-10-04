// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Samples in the formats a device may take as is, converted to and from
// the library's 32-bit float, as an exclusive stream needs.

#ifndef MAUL_AUDIO_SRC_SAMPLE_CONVERT_H
#define MAUL_AUDIO_SRC_SAMPLE_CONVERT_H

#include <stddef.h>
#include <stdint.h>

// A device's sample format. 24 valid bits in 32-bit containers are
// left-justified, so they convert as maud_sampleInt32.
typedef enum maudSampleKind
{
    maud_sampleFloat32 = 0,
    maud_sampleInt32 = 1,
    maud_sampleInt16 = 2,
} maudSampleKind;

// Bytes of one sample of kind.
size_t maudSampleBytes(maudSampleKind kind);

// Writes count samples of kind from floats: full scale is 1.0, values
// past it saturate, NaN is silence, integers round to the nearest.
void maudFloatToSamples(void* out, const float* in, size_t count, maudSampleKind kind);

// Reads count samples of kind into floats, full scale to 1.0.
void maudSamplesToFloat(float* out, const void* in, size_t count, maudSampleKind kind);

#endif // MAUL_AUDIO_SRC_SAMPLE_CONVERT_H
