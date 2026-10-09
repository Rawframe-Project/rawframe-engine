// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The echo canceller's suppressor: one gain per bin against noise and
// the residual echo the linear stage leaves, on frames of two blocks
// under a square-root Hann window, a block apart. The residual echo is
// the echo estimate's power times the leakage of its band (decaying no
// faster than a set rate); the noise is tracked on the output where
// speech is unlikely. Speech presence comes from a priori SNRs smoothed
// over Bark bands and a frame probability (as Speex's preprocessor
// takes it), the decision-directed estimate weighted by the last
// frame's level; the gain is the log-spectral amplitude estimate under
// that presence, floored by a mix of a noise floor and an echo floor
// the frame probability moves.

#ifndef MAUL_AUDIO_SRC_ECHO_SUPPRESSOR_H
#define MAUL_AUDIO_SRC_ECHO_SUPPRESSOR_H

#include "real_fft.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct maudEchoSuppressor
{
    // A block's frames and the transform's bins (block + 1); the Bark
    // bands, those below 8 kHz, and each bin's band.
    uint32_t block;
    uint32_t bins;
    uint32_t bands;
    uint32_t speechBands;
    uint16_t* band;
    uint32_t* perBand;
    // The transform of two blocks and its window.
    const maudRealFft* fft;
    float* window;
    // The last block of the output and of the echo estimate, the
    // synthesis' tail, and scratch: a frame, two spectra.
    float* lastOutput;
    float* lastEcho;
    float* tail;
    float* frame;
    float* spectrum;
    float* echoSpectrum;
    // Per bin: the residual echo, the noise, the last frame's speech
    // level, and this frame's power, interference, a posteriori and
    // a priori SNRs and presence.
    double* residual;
    double* noise;
    double* lastSpeech;
    double* power;
    double* interference;
    double* posterior;
    double* prior;
    // Per band: the smoothed a priori SNR, and this frame's sum.
    double* zeta;
    double* sum;
    // The floors: noise's and the echo's at the frame probability's two
    // ends, as power ratios.
    double noiseFloor;
    double echoFloorQuiet;
    double echoFloorSpeech;
    // Per-block constants scaled to the block's length.
    double residualDecay;
    double zetaKeep;
    double speechKeep;
    double noiseKeep;
    // Whether a frame has been analysed (the first sets the noise), and
    // whether a block has been seen (a frame takes two).
    bool started;
    bool primed;
    // The last frame's probability of speech.
    double frameProbability;
} maudEchoSuppressor;

// The bytes a suppressor of block frames (a power of two) at a rate
// takes, aligned for double.
size_t maudEchoSuppressorBytes(uint32_t block, double sampleRate);

// Lays a suppressor out in memory of maudEchoSuppressorBytes; fft is a
// plan for two blocks; floorDb is the noise's floor in dB.
void maudInitEchoSuppressor(maudEchoSuppressor* suppressor, uint32_t block, double sampleRate,
                            double floorDb, const maudRealFft* fft, void* memory);

// Runs a block of the linear stage's output and echo estimate, with its
// bands' leakage (bands of bandBins bins); writes the block before it,
// suppressed (the first block out is silence).
void maudRunEchoSuppressor(maudEchoSuppressor* suppressor, const float* output, const float* echo,
                           const float* bandLeak, uint32_t bandBins, float* out);

#endif // MAUL_AUDIO_SRC_ECHO_SUPPRESSOR_H
