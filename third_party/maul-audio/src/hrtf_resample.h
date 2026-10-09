// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Impulse responses moved to another sample rate, as an HRTF set loaded
// for a renderer at another rate needs.

#ifndef MAUL_AUDIO_SRC_HRTF_RESAMPLE_H
#define MAUL_AUDIO_SRC_HRTF_RESAMPLE_H

#include <stdint.h>

// The taps a response of taps at from takes at to, its lead included
// (maudResampleResponse); taps itself at the same rate.
uint32_t maudResampledTaps(uint32_t taps, uint32_t from, uint32_t to);

// Resamples a response of inTaps at from into outTaps at to, keeping its
// frequency response below both rates' Nyquist frequency: band-limited
// interpolation with a Blackman-windowed sinc 32 input samples wide on
// each side (wider when going down), scaled so the filter's gain is the
// same at the new rate. The output starts 24 input samples early: a
// minimum-phase response begins at full height, and its band-limited
// form rings before it, which a start at its first sample would cut off
// (11 dB of error at 96 kHz, measured). The lead is the same for every
// response, so interaural delays stay as they were.
void maudResampleResponse(const float* in, uint32_t inTaps, uint32_t from, float* out,
                          uint32_t outTaps, uint32_t to);

#endif // MAUL_AUDIO_SRC_HRTF_RESAMPLE_H
