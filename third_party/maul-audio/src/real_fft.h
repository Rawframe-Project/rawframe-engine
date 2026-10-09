// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// A real FFT in float for the audio thread: size n a power of two from 4
// to 65,536, a complex FFT of n / 2 (radix 2, its bit reversal folded
// into the loads and stores) and the split that turns it into n / 2 + 1
// bins. Its tables live in memory the caller provides, so a transform
// allocates nothing; a plan is read only, shared by any threads.

#ifndef MAUL_AUDIO_SRC_REAL_FFT_H
#define MAUL_AUDIO_SRC_REAL_FFT_H

#include <stddef.h>
#include <stdint.h>

typedef struct maudRealFft
{
    uint32_t size;
    // The complex FFT's twiddles e^(-2 pi i k / (n / 2)), k < n / 4, and
    // the split's e^(-2 pi i k / n), k < n / 2, interleaved.
    float* twiddles;
    float* split;
    // The bit reversal of n / 2 indices.
    uint32_t* reverse;
} maudRealFft;

// The bytes a plan's tables take.
size_t maudRealFftBytes(uint32_t size);

// Makes a plan for size in memory of maudRealFftBytes(size) bytes,
// aligned for float.
void maudInitRealFft(maudRealFft* fft, uint32_t size, void* memory);

// The n / 2 + 1 bins (interleaved real and imaginary parts) of n real
// samples: X[k] = sum over j of x[j] e^(-2 pi i j k / n).
void maudForwardRealFft(const maudRealFft* fft, const float* in, float* bins);

// The n real samples of n / 2 + 1 bins, divided by n: the inverse of
// maudForwardRealFft. bins is used as scratch and left changed.
void maudInverseRealFft(const maudRealFft* fft, float* bins, float* out);

#endif // MAUL_AUDIO_SRC_REAL_FFT_H
