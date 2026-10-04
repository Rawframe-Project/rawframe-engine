// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The voice activity detector's analysis, one mono sample at a time,
// shared by the detector and the gain control.

#ifndef MAUL_AUDIO_SRC_VAD_CORE_H
#define MAUL_AUDIO_SRC_VAD_CORE_H

#include "voice_dsp.h"

#include <stdbool.h>
#include <stdint.h>

// The classic VAD's six bands.
#define MAUD_VAD_BANDS 6
// The noise floor's windows of 30 frames: the current one and the last
// four, 1.5 s.
#define MAUD_VAD_WINDOWS 4

typedef struct maudVadCore
{
    // A fourth-order high-pass at 80 Hz, two sections.
    maudBiquad highPass[2];
    maudBiquad bands[MAUD_VAD_BANDS];
    // The frame being gathered: its samples, and the sums of squares of
    // each band and of the whole (after the high-pass).
    uint32_t frameSamples;
    uint32_t filled;
    double energy[MAUD_VAD_BANDS + 1];
    // Per band and for the whole: the smoothed level, the minimum of the
    // current window and of the last ones, and the floor they give.
    float smoothed[MAUD_VAD_BANDS + 1];
    float windowMin[MAUD_VAD_BANDS + 1];
    float windows[MAUD_VAD_BANDS + 1][MAUD_VAD_WINDOWS];
    float noise[MAUD_VAD_BANDS + 1];
    uint32_t windowFrames;
    uint32_t windowIndex;
    uint32_t windowsFilled;
    // The decision: thresholds in dB, frames needed to turn on and kept
    // on after speech, and the runs so far.
    float globalThreshold;
    float localThreshold;
    uint32_t onsetFrames;
    uint32_t hangoverFrames;
    uint32_t onsetRun;
    uint32_t hangoverLeft;
    // The last frame's results.
    bool active;
    bool speech;
    float probability;
    float level;
    uint64_t frames;
} maudVadCore;

// Sets a detector up for a rate, an aggressiveness (0 to 3) and a
// hangover in 10 ms frames.
void maudInitVadCore(maudVadCore* core, uint32_t sampleRate, uint8_t aggressiveness,
                     uint32_t hangoverFrames);

// Analyzes one sample; true when it completed a 10 ms frame, whose
// results the core then holds. Real-time safe.
bool maudVadSample(maudVadCore* core, float sample);

#endif // MAUL_AUDIO_SRC_VAD_CORE_H
