// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The order of an ALSA PCM's channels against a stream's layout.

#ifndef MAUL_AUDIO_SRC_ALSA_CHMAP_H
#define MAUL_AUDIO_SRC_ALSA_CHMAP_H

#include "maul-audio/layout.h"

#include <stdbool.h>
#include <stdint.h>

// The most channels a layout has.
#define MAUD_ALSA_MAX_CHANNELS 12u

// Finds, for each of the PCM's count channels at positions (SND_CHMAP_*
// values; NULL when the PCM reports none, which means ALSA's standard
// order), the stream channel it carries: orderOut[pcm] = stream. A side
// speaker stands in for a rear one and the reverse. False when a
// channel has no match.
bool maudAlsaChannelOrder(maudChannelLayout layout, const unsigned int* positions, uint32_t count,
                          uint8_t* orderOut);

// Whether order maps every channel to itself.
bool maudAlsaOrderIsIdentity(const uint8_t* order, uint32_t count);

#endif // MAUL_AUDIO_SRC_ALSA_CHMAP_H
