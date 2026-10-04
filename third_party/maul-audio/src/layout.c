// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The channel layouts: speakers in the Windows speaker mask order, with
// the nominal positions of Recommendation ITU-R BS.2051.

#include "layout.h"

#include "maul-audio/layout.h"

#define LAYOUT_MAX_CHANNELS 12

typedef struct LayoutChannel
{
    maudSpeaker speaker;
    float azimuth;
    float elevation;
} LayoutChannel;

typedef struct LayoutTable
{
    uint32_t channelCount;
    LayoutChannel channels[LAYOUT_MAX_CHANNELS];
} LayoutTable;

// Indexed by maudChannelLayout. The low-frequency channel has no
// direction, so its position stays zero.
static const LayoutTable s_layouts[] = {
    [maud_layoutNone] = {0, {{0}}},
    [maud_layoutMono] = {1, {{maud_speakerFrontCenter, 0.0f, 0.0f}}},
    [maud_layoutStereo] = {2,
                           {
                               {maud_speakerFrontLeft, 30.0f, 0.0f},
                               {maud_speakerFrontRight, -30.0f, 0.0f},
                           }},
    [maud_layoutQuad] = {4,
                         {
                             {maud_speakerFrontLeft, 45.0f, 0.0f},
                             {maud_speakerFrontRight, -45.0f, 0.0f},
                             {maud_speakerBackLeft, 135.0f, 0.0f},
                             {maud_speakerBackRight, -135.0f, 0.0f},
                         }},
    [maud_layout5Point1] = {6,
                            {
                                {maud_speakerFrontLeft, 30.0f, 0.0f},
                                {maud_speakerFrontRight, -30.0f, 0.0f},
                                {maud_speakerFrontCenter, 0.0f, 0.0f},
                                {maud_speakerLowFrequency, 0.0f, 0.0f},
                                {maud_speakerSideLeft, 110.0f, 0.0f},
                                {maud_speakerSideRight, -110.0f, 0.0f},
                            }},
    [maud_layout7Point1] = {8,
                            {
                                {maud_speakerFrontLeft, 30.0f, 0.0f},
                                {maud_speakerFrontRight, -30.0f, 0.0f},
                                {maud_speakerFrontCenter, 0.0f, 0.0f},
                                {maud_speakerLowFrequency, 0.0f, 0.0f},
                                {maud_speakerBackLeft, 135.0f, 0.0f},
                                {maud_speakerBackRight, -135.0f, 0.0f},
                                {maud_speakerSideLeft, 90.0f, 0.0f},
                                {maud_speakerSideRight, -90.0f, 0.0f},
                            }},
    [maud_layout7Point1Point4] = {12,
                                  {
                                      {maud_speakerFrontLeft, 30.0f, 0.0f},
                                      {maud_speakerFrontRight, -30.0f, 0.0f},
                                      {maud_speakerFrontCenter, 0.0f, 0.0f},
                                      {maud_speakerLowFrequency, 0.0f, 0.0f},
                                      {maud_speakerBackLeft, 135.0f, 0.0f},
                                      {maud_speakerBackRight, -135.0f, 0.0f},
                                      {maud_speakerSideLeft, 90.0f, 0.0f},
                                      {maud_speakerSideRight, -90.0f, 0.0f},
                                      {maud_speakerTopFrontLeft, 45.0f, 30.0f},
                                      {maud_speakerTopFrontRight, -45.0f, 30.0f},
                                      {maud_speakerTopBackLeft, 135.0f, 30.0f},
                                      {maud_speakerTopBackRight, -135.0f, 30.0f},
                                  }},
};

static const LayoutChannel* FindChannel(maudChannelLayout layout, uint32_t channel)
{
    if (layout >= sizeof(s_layouts) / sizeof(s_layouts[0]))
    {
        return nullptr;
    }
    const LayoutTable* table = &s_layouts[layout];
    if (channel >= table->channelCount)
    {
        return nullptr;
    }
    return &table->channels[channel];
}

uint32_t maudGetLayoutChannelCount(maudChannelLayout layout)
{
    if (layout >= sizeof(s_layouts) / sizeof(s_layouts[0]))
    {
        return 0;
    }
    return s_layouts[layout].channelCount;
}

maudSpeaker maudGetLayoutSpeaker(maudChannelLayout layout, uint32_t channel)
{
    const LayoutChannel* found = FindChannel(layout, channel);
    return found != nullptr ? found->speaker : maud_speakerNone;
}

maudSpeakerPosition maudGetLayoutSpeakerPosition(maudChannelLayout layout, uint32_t channel)
{
    const LayoutChannel* found = FindChannel(layout, channel);
    if (found == nullptr)
    {
        return (maudSpeakerPosition){0.0f, 0.0f};
    }
    return (maudSpeakerPosition){found->azimuth, found->elevation};
}

maudChannelLayout maudLayoutWithChannels(uint32_t channels)
{
    for (uint32_t layout = maud_layoutMono; layout <= maud_layout7Point1Point4; ++layout)
    {
        if (s_layouts[layout].channelCount == channels)
        {
            return (maudChannelLayout)layout;
        }
    }
    return maud_layoutNone;
}
