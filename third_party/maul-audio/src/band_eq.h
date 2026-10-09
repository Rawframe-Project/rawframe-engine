// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// A three-band equalizer: a low shelf at 800 Hz, a bell at the middle
// band's geometric centre and a high shelf at 8 kHz, as state-variable
// filters in their topology-preserving form, whose coefficients may ramp
// sample by sample. Band targets (mean dB over each band) become filter
// gains through an inverted interaction matrix and a few refinements
// against the filters' actual band means; the setup per sample rate and
// the solve run off the audio thread's hot path, the filters on it.

#ifndef MAUL_AUDIO_SRC_BAND_EQ_H
#define MAUL_AUDIO_SRC_BAND_EQ_H

#include <stdint.h>

#define MAUD_BANDS 3
// Points per band at which band means are taken while solving.
#define MAUD_BAND_POINTS 8

// What the solve needs at one sample rate.
typedef struct maudBandEqSetup
{
    float sampleRate;
    // Each filter's prewarped frequency, tan(pi f / rate).
    double warped[MAUD_BANDS];
    // Each band's points, prewarped likewise.
    double points[MAUD_BANDS][MAUD_BAND_POINTS];
    // The inverse of the interaction matrix: filter gains from band
    // targets, both in dB.
    double inverse[MAUD_BANDS][MAUD_BANDS];
} maudBandEqSetup;

// The three filters' coefficients.
typedef struct maudBandEqFilters
{
    float g[MAUD_BANDS];
    float k[MAUD_BANDS];
    float m0[MAUD_BANDS];
    float m1[MAUD_BANDS];
    float m2[MAUD_BANDS];
} maudBandEqFilters;

// The filters' state.
typedef struct maudBandEqState
{
    float ic1[MAUD_BANDS];
    float ic2[MAUD_BANDS];
} maudBandEqState;

// Fills setup for a rate from 32 kHz to 384 kHz.
void maudSetupBandEq(maudBandEqSetup* setup, float sampleRate);

// The mean dB over each band of filters with the given gains (dB), at
// the setup's points.
void maudBandEqMeans(const maudBandEqSetup* setup, const double gains[MAUD_BANDS],
                     double means[MAUD_BANDS]);

// The filter gains (dB) whose band means meet targets (dB): the
// interaction matrix's estimate refined three times.
void maudSolveBandEq(const maudBandEqSetup* setup, const double targets[MAUD_BANDS],
                     double gains[MAUD_BANDS]);

// The coefficients of filters with the given gains (dB).
void maudDesignBandEq(const maudBandEqSetup* setup, const double gains[MAUD_BANDS],
                      maudBandEqFilters* filters);

// Runs frames through the filters, in place allowed; the coefficients
// move linearly from `from` to `to` in steps every 8 frames, each step
// taking the coefficients at its last frame, so the call ends on `to`.
// The same pointer for both skips the ramp.
void maudRunBandEq(maudBandEqState* state, const maudBandEqFilters* from,
                   const maudBandEqFilters* to, const float* in, float* out, uint32_t frames);

#endif // MAUL_AUDIO_SRC_BAND_EQ_H
