// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// A response from an energy field (reverb_estimate.h), shaped in
// frequency: per 10 ms bin a frame of complex noise from a fixed
// sequence, its magnitude at each FFT bin the bin's band energies
// interpolated in log energy over log frequency (held past the end
// bands' centres), sine-windowed over two hops and overlap-added
// centred on the bin, so that each sample's expected power is the bin's
// energy over its hop. The channels share the noise, each scaled per
// band by its ratio to W, the ratios interpolated linearly in log
// frequency: three frames per bin whatever the order. Built on the
// simulation side, never on the audio thread.

#ifndef MAUL_AUDIO_SRC_REFLECTION_RESPONSE_H
#define MAUL_AUDIO_SRC_REFLECTION_RESPONSE_H

#include <stddef.h>
#include <stdint.h>

// The samples a 10 ms bin spans at a rate.
uint32_t maudResponseHop(double rate);

// The doubles of scratch a reconstruction at a rate needs.
size_t maudResponseScratch(double rate);

// Reconstructs (order + 1)^2 channels of frames samples into out (each
// overwritten) from a field of that order: channels of three bands of
// bins bins, as the tracer lays it out. Bins past frames are dropped;
// samples past the bins are silent.
void maudReconstructResponse(const float* field, uint32_t order, uint32_t bins, double rate,
                             float* const* out, uint32_t frames, double* scratch);

#endif // MAUL_AUDIO_SRC_REFLECTION_RESPONSE_H
