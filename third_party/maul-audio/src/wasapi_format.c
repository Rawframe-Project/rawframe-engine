// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Speaker masks. The layouts' channels are already in the mask's bit
// order, so a mask is the OR of their speakers' bits.

#include "wasapi_format.h"

// The SPEAKER_* bit of each speaker, from ksmedia.h.
static uint32_t BitOf(maudSpeaker speaker)
{
    static const uint32_t bits[] = {
        [maud_speakerNone] = 0,
        [maud_speakerFrontLeft] = 0x1,
        [maud_speakerFrontRight] = 0x2,
        [maud_speakerFrontCenter] = 0x4,
        [maud_speakerLowFrequency] = 0x8,
        [maud_speakerBackLeft] = 0x10,
        [maud_speakerBackRight] = 0x20,
        [maud_speakerSideLeft] = 0x200,
        [maud_speakerSideRight] = 0x400,
        [maud_speakerTopFrontLeft] = 0x1000,
        [maud_speakerTopFrontRight] = 0x4000,
        [maud_speakerTopBackLeft] = 0x8000,
        [maud_speakerTopBackRight] = 0x20000,
    };
    return bits[speaker];
}

uint32_t maudWasapiMaskOfLayout(maudChannelLayout layout)
{
    uint32_t channels = maudGetLayoutChannelCount(layout);
    if (channels == 1)
    {
        return BitOf(maud_speakerFrontCenter);
    }
    uint32_t mask = 0;
    for (uint32_t c = 0; c < channels; ++c)
    {
        mask |= BitOf(maudGetLayoutSpeaker(layout, c));
    }
    return mask;
}
