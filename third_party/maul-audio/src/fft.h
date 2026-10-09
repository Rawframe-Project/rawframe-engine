// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// A complex FFT in double precision for building filters at creation
// time (never on the audio thread): iterative radix 2, in place, for a
// power-of-two size.

#ifndef MAUL_AUDIO_SRC_FFT_H
#define MAUL_AUDIO_SRC_FFT_H

#include <stdint.h>

// The transform of count complex values, interleaved as real and
// imaginary parts, in place: X[k] = sum over n of x[n] e^(-2 pi i k n /
// count), or with +i and divided by count when inverse. count is a power
// of two, at least 2.
void maudFft(double* values, uint32_t count, bool inverse);

#endif // MAUL_AUDIO_SRC_FFT_H
