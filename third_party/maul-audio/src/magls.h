// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Binaural decoders for an ambisonic bed by magnitude least squares
// (Schorkhuber, Zaunschirm and Holdrich, 2018): least squares below
// 1.5 kHz, magnitudes alone above it with the phase continued from the
// bin below and advanced by a group delay, which keeps the filters
// causal. Built once, with the allocator's memory for the work.

#ifndef MAUL_AUDIO_SRC_MAGLS_H
#define MAUL_AUDIO_SRC_MAGLS_H

#include "hrtf_core.h"

#include <stdint.h>

// The transform size the build uses for a set, a filter length and a
// group delay in samples: a power of two that holds a response, its
// largest delay, the group delay and a filter, and no less than 512 at
// 48 kHz (in proportion).
uint32_t maudMagLsSize(const maudHrtf* hrtf, uint32_t taps, double groupDelay);

// Each direction's share of the sphere, in the set's order: its ring's
// band, from the midpoints to the neighbouring rings (or the pole), over
// the ring's azimuths; the shares add up to 1.
void maudRingWeights(const maudHrtf* hrtf, double* weights);

// Fills filters, 2 ears x (order + 1)^2 channels x taps (ear-major),
// with the decoder of an order, 1 to 3, for a set at its rate; the last
// fadeTaps of each fade out. maud_errorCapacity when the allocator
// fails; maud_errorInvalid when the set's directions cannot carry the
// order (the normal matrix is singular).
maudResult maudBuildMagLs(const maudHrtf* hrtf, uint32_t order, uint32_t taps, double groupDelay,
                          uint32_t fadeTaps, const maudAllocator* allocator, float* filters);

#endif // MAUL_AUDIO_SRC_MAGLS_H
