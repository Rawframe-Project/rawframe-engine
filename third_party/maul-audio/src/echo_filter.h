// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The echo canceller's linear stage: a multidelay block frequency-domain
// adaptive filter (Soo and Pang 1990) of a block's frames per partition,
// whose rates per bin follow the leakage of the echo into the error
// (Valin 2007), with steps proportionate across partitions and two
// paths: a background filter adapts, a foreground one outputs, and a
// statistical test hands the background's over or takes it back.

#ifndef MAUL_AUDIO_SRC_ECHO_FILTER_H
#define MAUL_AUDIO_SRC_ECHO_FILTER_H

#include "real_fft.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// The leakage is also kept per band of this many hertz, for the
// suppressor.
#define MAUD_ECHO_BAND_HZ 500.0

typedef struct maudEchoFilter
{
    // A block's frames, its transform's bins (block + 1), the partitions,
    // the bands of MAUD_ECHO_BAND_HZ and the bins per band.
    uint32_t block;
    uint32_t bins;
    uint32_t partitions;
    uint32_t bands;
    uint32_t binsPerBand;
    // The transform of twice a block.
    const maudRealFft* fft;
    // The render's spectra, partitions of bins complex values (real and
    // imaginary interleaved), the newest at head; the background and
    // foreground filters' in the same layout, partition 0 the newest.
    float* render;
    float* background;
    float* foreground;
    uint32_t head;
    // The render's last block; scratch: twice a block, bins complex
    // values, the error's and the echo estimate's spectra, and in time
    // the two estimates (twice a block each) and the background's error.
    float* last;
    float* time;
    float* spectrum;
    float* errorSpectrum;
    float* echoSpectrum;
    float* backgroundEcho;
    float* foregroundEcho;
    float* backgroundError;
    // Per bin: the render's power smoothed over the filter's length, and
    // the error's and the echo estimate's recent powers (the leakage's
    // regression is on their changes).
    double* slowPower;
    double* errorRecent;
    double* echoRecent;
    // Per band: the regression's sums this block (covariance, then
    // variance), its smoothed sums, and the leakage.
    double* bandNow;
    double* bandCovariance;
    double* bandVariance;
    float* bandLeak;
    // Per partition: its share of the step.
    double* shares;
    // The leakage and its regression's sums.
    double leak;
    double covariance;
    double variance;
    // The two paths' test: the error's drop and its noise, over two
    // windows.
    double drop1;
    double drop2;
    double noise1;
    double noise2;
    // Until it has adapted, the start rate's sum.
    double started;
    bool adapted;
    // The partition the gradient constraint takes next.
    uint32_t constrain;
    // The per-block smoothing constants, scaled to the block's length.
    double recent;
    double window1;
    double window2;
} maudEchoFilter;

// The bytes a filter of partitions of block frames takes (block a power
// of two), aligned for double.
size_t maudEchoFilterBytes(uint32_t block, uint32_t partitions, double sampleRate);

// Lays a filter out in memory of maudEchoFilterBytes, silent and
// unadapted; fft is a plan for twice the block.
void maudInitEchoFilter(maudEchoFilter* filter, uint32_t block, uint32_t partitions,
                        double sampleRate, const maudRealFft* fft, void* memory);

// Forgets everything learnt.
void maudResetEchoFilter(maudEchoFilter* filter);

// Runs a block: the render played and the capture it reached, both a
// block's frames; writes the foreground's error (the capture less its
// echo estimate) and the estimate.
void maudRunEchoFilter(maudEchoFilter* filter, const float* render, const float* capture,
                       float* error, float* echo);

#endif // MAUL_AUDIO_SRC_ECHO_FILTER_H
