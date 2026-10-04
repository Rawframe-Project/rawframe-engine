// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Channel layouts: which speaker each channel of an interleaved frame
// feeds, and where that speaker stands.

#ifndef MAUL_AUDIO_LAYOUT_H
#define MAUL_AUDIO_LAYOUT_H

#include "maul-audio/base.h"

#ifdef __cplusplus
extern "C"
{
#endif

    // A channel layout. Channels are in the Windows speaker mask order,
    // the order WAV files use; backends reorder them for platforms that
    // differ. Positions are the nominal ones of Recommendation ITU-R
    // BS.2051.
    typedef uint8_t maudChannelLayout;

    enum
    {
        // Not a layout. Every layout query on it reports nothing.
        maud_layoutNone = 0,
        // One channel: front center.
        maud_layoutMono = 1,
        // Front left and right, at 30 degrees.
        maud_layoutStereo = 2,
        // Front left and right at 45 degrees, back left and right at 135.
        maud_layoutQuad = 3,
        // Front left, front right, front center, low frequency, side left
        // and side right, the sides at 110 degrees (BS.2051 system B).
        maud_layout5Point1 = 4,
        // Front left, front right, front center, low frequency, back left,
        // back right, side left and side right; backs at 135 degrees, sides
        // at 90 (BS.2051 system I).
        maud_layout7Point1 = 5,
        // 7.1 followed by top front left, top front right, top back left and
        // top back right, at 45 and 135 degrees and 30 degrees up (BS.2051
        // system J).
        maud_layout7Point1Point4 = 6,
    };

    // The speaker a channel feeds.
    typedef uint8_t maudSpeaker;

    enum
    {
        // Not a speaker: the channel is out of the layout's range.
        maud_speakerNone = 0,
        maud_speakerFrontLeft = 1,
        maud_speakerFrontRight = 2,
        maud_speakerFrontCenter = 3,
        // The low-frequency channel, which has no direction.
        maud_speakerLowFrequency = 4,
        maud_speakerBackLeft = 5,
        maud_speakerBackRight = 6,
        maud_speakerSideLeft = 7,
        maud_speakerSideRight = 8,
        maud_speakerTopFrontLeft = 9,
        maud_speakerTopFrontRight = 10,
        maud_speakerTopBackLeft = 11,
        maud_speakerTopBackRight = 12,
    };

    // Where a speaker stands, seen from the listener. Azimuth is positive
    // to the left and 0 straight ahead; elevation is positive upward.
    typedef struct maudSpeakerPosition
    {
        float azimuthDegrees;
        float elevationDegrees;
    } maudSpeakerPosition;

    /// Returns the number of channels in a layout.
    ///
    /// @param layout  A layout.
    /// @return The channel count, or 0 for `maud_layoutNone` and values that
    ///         name no layout.
    /// @par Thread safety
    /// Safe from any thread.
    MAUD_API uint32_t maudGetLayoutChannelCount(maudChannelLayout layout);

    /// Returns the speaker one channel of a layout feeds.
    ///
    /// @param layout   A layout.
    /// @param channel  The channel's index in an interleaved frame.
    /// @return The speaker, or `maud_speakerNone` when the layout names no
    ///         layout or the channel is out of its range.
    /// @par Thread safety
    /// Safe from any thread.
    MAUD_API maudSpeaker maudGetLayoutSpeaker(maudChannelLayout layout, uint32_t channel);

    /// Returns the nominal position of the speaker one channel of a layout
    /// feeds.
    ///
    /// @param layout   A layout.
    /// @param channel  The channel's index in an interleaved frame.
    /// @return The position. It is zero for the low-frequency channel, which
    ///         has no direction, and when the layout names no layout or the
    ///         channel is out of its range.
    /// @par Thread safety
    /// Safe from any thread.
    MAUD_API maudSpeakerPosition maudGetLayoutSpeakerPosition(maudChannelLayout layout,
                                                              uint32_t channel);

#ifdef __cplusplus
}
#endif

#endif // MAUL_AUDIO_LAYOUT_H
