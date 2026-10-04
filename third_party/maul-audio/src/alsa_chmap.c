// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Channel maps. ALSA names positions with SND_CHMAP_* and, for a PCM
// that reports no map, uses the standard surround order of its
// configuration files: front, rear, center and LFE, then side.

#include "alsa_chmap.h"

#include <alsa/asoundlib.h>

// The SND_CHMAP_* position of each speaker.
static unsigned int PositionOf(maudSpeaker speaker)
{
    static const unsigned int positions[] = {
        [maud_speakerNone] = SND_CHMAP_UNKNOWN,      [maud_speakerFrontLeft] = SND_CHMAP_FL,
        [maud_speakerFrontRight] = SND_CHMAP_FR,     [maud_speakerFrontCenter] = SND_CHMAP_FC,
        [maud_speakerLowFrequency] = SND_CHMAP_LFE,  [maud_speakerBackLeft] = SND_CHMAP_RL,
        [maud_speakerBackRight] = SND_CHMAP_RR,      [maud_speakerSideLeft] = SND_CHMAP_SL,
        [maud_speakerSideRight] = SND_CHMAP_SR,      [maud_speakerTopFrontLeft] = SND_CHMAP_TFL,
        [maud_speakerTopFrontRight] = SND_CHMAP_TFR, [maud_speakerTopBackLeft] = SND_CHMAP_TRL,
        [maud_speakerTopBackRight] = SND_CHMAP_TRR,
    };
    return positions[speaker];
}

// The position that stands in for another: side for rear and rear for
// side; otherwise none.
static unsigned int Stand(unsigned int position)
{
    switch (position)
    {
    case SND_CHMAP_RL:
        return SND_CHMAP_SL;
    case SND_CHMAP_RR:
        return SND_CHMAP_SR;
    case SND_CHMAP_SL:
        return SND_CHMAP_RL;
    case SND_CHMAP_SR:
        return SND_CHMAP_RR;
    default:
        return SND_CHMAP_UNKNOWN;
    }
}

// ALSA's standard order for count channels, or NULL for a count it
// defines none for, where the order is taken as the stream's own.
static const unsigned int* StandardOrder(uint32_t count)
{
    static const unsigned int one[] = {SND_CHMAP_MONO};
    static const unsigned int two[] = {SND_CHMAP_FL, SND_CHMAP_FR};
    static const unsigned int four[] = {SND_CHMAP_FL, SND_CHMAP_FR, SND_CHMAP_RL, SND_CHMAP_RR};
    static const unsigned int six[] = {SND_CHMAP_FL, SND_CHMAP_FR, SND_CHMAP_RL,
                                       SND_CHMAP_RR, SND_CHMAP_FC, SND_CHMAP_LFE};
    static const unsigned int eight[] = {SND_CHMAP_FL, SND_CHMAP_FR,  SND_CHMAP_RL, SND_CHMAP_RR,
                                         SND_CHMAP_FC, SND_CHMAP_LFE, SND_CHMAP_SL, SND_CHMAP_SR};
    switch (count)
    {
    case 1:
        return one;
    case 2:
        return two;
    case 4:
        return four;
    case 6:
        return six;
    case 8:
        return eight;
    default:
        return nullptr;
    }
}

// The stream channel at position, not yet taken, or count.
static uint32_t Find(maudChannelLayout layout, uint32_t count, unsigned int position,
                     const bool* taken)
{
    for (uint32_t c = 0; c < count; ++c)
    {
        unsigned int own =
            count == 1 ? (unsigned int)SND_CHMAP_MONO : PositionOf(maudGetLayoutSpeaker(layout, c));
        if (!taken[c] && own == position)
        {
            return c;
        }
    }
    return count;
}

bool maudAlsaChannelOrder(maudChannelLayout layout, const unsigned int* positions, uint32_t count,
                          uint8_t* orderOut)
{
    if (count > MAUD_ALSA_MAX_CHANNELS || count != maudGetLayoutChannelCount(layout))
    {
        return false;
    }
    if (positions == nullptr)
    {
        positions = StandardOrder(count);
    }
    if (positions == nullptr)
    {
        for (uint32_t c = 0; c < count; ++c)
        {
            orderOut[c] = (uint8_t)c;
        }
        return true;
    }
    bool taken[MAUD_ALSA_MAX_CHANNELS] = {false};
    for (uint32_t pcm = 0; pcm < count; ++pcm)
    {
        uint32_t c = Find(layout, count, positions[pcm], taken);
        c = c < count ? c : Find(layout, count, Stand(positions[pcm]), taken);
        if (c == count)
        {
            return false;
        }
        taken[c] = true;
        orderOut[pcm] = (uint8_t)c;
    }
    return true;
}

bool maudAlsaOrderIsIdentity(const uint8_t* order, uint32_t count)
{
    for (uint32_t c = 0; c < count; ++c)
    {
        if (order[c] != c)
        {
            return false;
        }
    }
    return true;
}
