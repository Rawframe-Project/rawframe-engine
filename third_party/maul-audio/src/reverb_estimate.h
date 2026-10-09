// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Reverberation times from geometry: rays from the listener; at
// each hit a shadow ray back adds the reflected energy into 10 ms bins
// per band (and, if asked, on spherical harmonics of its arrival
// direction in world axes: the energy field a response is made from); each band's histogram is
// integrated backwards and fitted from -5 to -25 dB. Rays go out in batches of 64, each batch into
// its own histogram, so that the batches can run on any threads and the sum, taken in batch order,
// is the same every time.

#ifndef MAUL_AUDIO_SRC_REVERB_ESTIMATE_H
#define MAUL_AUDIO_SRC_REVERB_ESTIMATE_H

#include "maul-audio/spatializer.h"

#include <stdint.h>

#define MAUD_REVERB_BATCH 64u
// Bins of 10 ms: 10 s, past the -25 dB of the longest time (20 s).
#define MAUD_REVERB_BINS 1000u

typedef struct maudReverbTrace
{
    maudClosestHitFn* closestHit;
    maudAnyHitFn* anyHit;
    void* context;
    const maudAcousticMaterial* materials;
    uint32_t materialCount;
    // The air's amplitude exponent per metre and band.
    float air[MAUD_DIRECT_BANDS];
    maudVector3 listener;
    // All rays (a multiple of the batch or not); bounces at most.
    uint32_t rays;
    uint32_t maxBounces;
    // The energy field's order (0 for none, else 1 to 3) and its bins.
    uint32_t fieldOrder;
    uint32_t fieldBins;
} maudReverbTrace;

// One batch's energy per band and bin, and when its first ray was cut
// short by the bounce cap (INFINITY if none was): past that, energy is
// missing.
// The energy is for a source at the listener emitting unit energy in
// every band: a bin's energy is what reaches the listener in it.
typedef struct maudReverbHistogram
{
    float energy[MAUD_DIRECT_BANDS][MAUD_REVERB_BINS];
    float truncated;
    // The batch's field if the trace has one: (fieldOrder + 1)^2
    // channels of MAUD_DIRECT_BANDS bands of fieldBins bins, SN3D in the
    // world's axes taken as a listener's (x right, y up, -z ahead).
    float* field;
} maudReverbHistogram;

// The batches rays take.
uint32_t maudReverbBatches(uint32_t rays);

// Traces batch (rays batch * 64 onward) into histogram, overwriting it
// (its field too, if the trace has one).
void maudTraceReverbBatch(const maudReverbTrace* trace, uint32_t batch,
                          maudReverbHistogram* histogram);

// Each band's decay: one slope, or a fast one and the slower one a
// coupled space adds (its tail), with the tail's share of the decay's
// energy (its backward integral) at the start; a band of one slope has a
// tail time and share of 0.
typedef struct maudReverbFit
{
    float times[MAUD_DIRECT_BANDS];
    float tailTimes[MAUD_DIRECT_BANDS];
    float tailShares[MAUD_DIRECT_BANDS];
} maudReverbFit;

// Sums count histograms into the first, in order, and fits each band:
// times in seconds, 0.1 to 20; the floor for a band without energy (an
// open field), the ceiling for one that never falls 25 dB. Where rays
// were cut short, the bins from the cut on are filled at the rate the
// bins before it decay at (as ISO 3382 part 1 compensates a truncated
// decay), and a band whose bins do not decay takes the ceiling. One
// slope is fitted from -5 to -25 dB; two replace it where one
// exponential misses the decay to -40 dB by more than 1.5 dB and two
// halve that, their times half again apart or more, the slower with 0.1 %
// of the energy or more.
void maudFitReverb(maudReverbHistogram* histograms, uint32_t count, maudReverbFit* fit);

// The levels (dB, -96 to 24) that match the reverb, whose W gives 10 ms
// bins of 0.0144 exp(-13.8 (t - delay) / T60) for a unit impulse, to the
// traced energy (a summed histogram) at time at: the mean of the bins in
// the 50 ms before it, split between the slopes as the fit's are there
// and carried to it by each slope's time; -96 for a tail there is not.
void maudReverbLevels(const maudReverbHistogram* summed, const maudReverbFit* fit, float at,
                      float delay, float* levels, float* tailLevels);

// Sums count histograms' fields into the first's, in order.
void maudSumReverbFields(const maudReverbTrace* trace, maudReverbHistogram* histograms,
                         uint32_t count);

#endif // MAUL_AUDIO_SRC_REVERB_ESTIMATE_H
