// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The voice activity detector's analysis, after WebRTC's classic VAD:
// six band levels from band-pass filters after a high-pass, each against
// its own noise floor, the minimum of its smoothed level over 1.5 s. A
// frame is speech when the weighted mean of the bands' margins over
// their floors passes the global threshold, or one band's above 250 Hz
// passes the local one (rumble lives below, and voice never only there);
// the decision turns on after an onset and stays on for a hangover.

#include "vad_core.h"

#include <math.h>

// Band edges in hertz, and their weights: the higher bands, where speech
// stands out from most noise, weigh more.
static const double s_edges[MAUD_VAD_BANDS + 1] = {80.0,   250.0,  500.0, 1000.0,
                                                   2000.0, 3000.0, 4000.0};
static const float s_weights[MAUD_VAD_BANDS] = {6.0f, 8.0f, 10.0f, 12.0f, 14.0f, 16.0f};
// Per aggressiveness: the thresholds in dB and the onset in frames.
static const float s_global[4] = {4.0f, 5.0f, 6.5f, 8.0f};
static const float s_local[4] = {10.0f, 11.0f, 13.0f, 15.0f};
static const uint32_t s_onset[4] = {1, 1, 2, 2};
// The smoothing of band levels per frame, and the window length.
#define SMOOTHING     0.6f
#define WINDOW_FRAMES 30u

void maudInitVadCore(maudVadCore* core, uint32_t sampleRate, uint8_t aggressiveness,
                     uint32_t hangoverFrames)
{
    *core = (maudVadCore){
        .frameSamples = sampleRate / 100,
        .globalThreshold = s_global[aggressiveness],
        .localThreshold = s_local[aggressiveness],
        .onsetFrames = s_onset[aggressiveness],
        .hangoverFrames = hangoverFrames,
        .level = MAUD_FLOOR_DBFS,
    };
    maudDesignHighPass(&core->highPass[0], 80.0, sampleRate);
    maudDesignHighPass(&core->highPass[1], 80.0, sampleRate);
    for (uint32_t b = 0; b < MAUD_VAD_BANDS; ++b)
    {
        maudDesignBandPass(&core->bands[b], s_edges[b], s_edges[b + 1], sampleRate);
    }
}

// Tracks one level's floor: the minimum of its smoothed value over the
// current window and the last MAUD_VAD_WINDOWS, rising only as old
// windows leave.
static void TrackFloor(maudVadCore* core, uint32_t index, float level)
{
    bool first = core->frames == 0;
    core->smoothed[index] =
        first ? level : SMOOTHING * core->smoothed[index] + (1.0f - SMOOTHING) * level;
    float smoothed = core->smoothed[index];
    if (core->windowFrames == 0 || smoothed < core->windowMin[index])
    {
        core->windowMin[index] = smoothed;
    }
    float floor = core->windowMin[index];
    for (uint32_t w = 0; w < core->windowsFilled; ++w)
    {
        floor = core->windows[index][w] < floor ? core->windows[index][w] : floor;
    }
    core->noise[index] = floor;
}

// Ends a window every WINDOW_FRAMES frames, dropping the oldest.
static void AdvanceWindow(maudVadCore* core)
{
    if (++core->windowFrames < WINDOW_FRAMES)
    {
        return;
    }
    for (uint32_t i = 0; i <= MAUD_VAD_BANDS; ++i)
    {
        core->windows[i][core->windowIndex] = core->windowMin[i];
    }
    core->windowIndex = (core->windowIndex + 1) % MAUD_VAD_WINDOWS;
    core->windowsFilled += core->windowsFilled < MAUD_VAD_WINDOWS ? 1u : 0u;
    core->windowFrames = 0;
}

// Decides a whole frame from its band energies.
static void Decide(maudVadCore* core)
{
    float margin = 0.0f;
    float weights = 0.0f;
    float widest = 0.0f;
    for (uint32_t i = 0; i <= MAUD_VAD_BANDS; ++i)
    {
        float level = maudLevelDbfs(core->energy[i] / core->frameSamples);
        TrackFloor(core, i, level);
        float above = level - core->noise[i];
        above = above > 0.0f ? above : 0.0f;
        if (i < MAUD_VAD_BANDS)
        {
            margin += s_weights[i] * above;
            weights += s_weights[i];
            widest = i > 0 && above > widest ? above : widest;
        }
        else
        {
            core->level = level;
        }
        core->energy[i] = 0.0;
    }
    AdvanceWindow(core);
    margin /= weights;
    core->probability = 1.0f / (1.0f + expf(-(margin - core->globalThreshold)));
    core->speech = margin > core->globalThreshold || widest > core->localThreshold;
    core->onsetRun = core->speech ? core->onsetRun + 1 : 0;
    if (core->speech && core->onsetRun >= core->onsetFrames)
    {
        core->active = true;
        core->hangoverLeft = core->hangoverFrames;
    }
    else if (!core->speech && core->active)
    {
        core->active = core->hangoverLeft > 0;
        core->hangoverLeft -= core->hangoverLeft > 0 ? 1u : 0u;
    }
    core->frames++;
}

bool maudVadSample(maudVadCore* core, float sample)
{
    float passed = maudFilter(&core->highPass[1], maudFilter(&core->highPass[0], sample));
    for (uint32_t b = 0; b < MAUD_VAD_BANDS; ++b)
    {
        float band = maudFilter(&core->bands[b], passed);
        core->energy[b] += (double)band * (double)band;
    }
    core->energy[MAUD_VAD_BANDS] += (double)passed * (double)passed;
    if (++core->filled < core->frameSamples)
    {
        return false;
    }
    core->filled = 0;
    Decide(core);
    return true;
}
