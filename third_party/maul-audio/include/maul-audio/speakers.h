// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Panning to speakers: a panner per channel layout, made once from the
// layout's nominal positions, pans sources by vector base amplitude
// panning (VBAP) into the layout's channels. Once made it is read-only:
// any number of sources share it, from any thread. Panning allocates
// nothing and does work in proportion to its frames.

#ifndef MAUL_AUDIO_SPEAKERS_H
#define MAUL_AUDIO_SPEAKERS_H

#include "maul-audio/base.h"
#include "maul-audio/layout.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    // A panner for one channel layout.
    typedef struct maudSpeakerPanner maudSpeakerPanner;

    // How to create a speaker panner. Build it with
    // maudDefaultSpeakerPannerDef.
    typedef struct maudSpeakerPannerDef
    {
        uint32_t cookie;
        // The layout panned into.
        maudChannelLayout layout;
        maudAllocator allocator;
    } maudSpeakerPannerDef;

    /// Returns the default speaker panner def: stereo.
    ///
    /// @return The def, with a valid cookie.
    /// @par Thread safety
    /// Safe from any thread.
    MAUD_API maudSpeakerPannerDef maudDefaultSpeakerPannerDef(void);

    /// Creates a speaker panner. Mono passes a source through; stereo pans
    /// by how far to the side a source is (one behind counts as in front);
    /// other layouts by VBAP over the triangles between their speakers,
    /// with imaginary speakers above and below whose share goes to their
    /// real neighbours. Gains keep the energy; the low-frequency channel
    /// gets nothing.
    ///
    /// @param def         The def, from maudDefaultSpeakerPannerDef.
    /// @param pannerOut   Receives the panner; NULL on failure.
    /// @return `maud_success`; `maud_errorInvalid` for a NULL pointer, a
    ///         def without its cookie or a layout the library does not
    ///         have; `maud_errorCapacity` when the allocator fails.
    /// @par Thread safety
    /// Safe from any thread.
    MAUD_NODISCARD MAUD_API maudResult maudCreateSpeakerPanner(const maudSpeakerPannerDef* def,
                                                               maudSpeakerPanner** pannerOut);

    /// Destroys a speaker panner. NULL is ignored.
    ///
    /// @param panner  The panner.
    /// @par Thread safety
    /// Safe from any thread; the panner is used by one thread at a time.
    MAUD_API void maudDestroySpeakerPanner(maudSpeakerPanner* panner);

    /// Computes a direction's gain for every channel of the layout.
    ///
    /// @param panner     The panner.
    /// @param direction  The direction, in the listener's frame.
    /// @param gainsOut   Receives the layout's channel count of gains.
    /// @return `maud_success`, or `maud_errorInvalid` for a NULL pointer or
    ///         a direction that is not finite.
    /// @par Thread safety
    /// Safe from any thread.
    MAUD_NODISCARD MAUD_API maudResult maudGetSpeakerGains(const maudSpeakerPanner* panner,
                                                           maudVector3 direction, float* gainsOut);

    /// Adds a mono source into the layout's channels, its direction and
    /// gain moving linearly from `from` to `to` across the call: frame n
    /// uses the gains (n + 1) / frames of the way.
    ///
    /// @param panner  The panner.
    /// @param from    The source at the previous call's end.
    /// @param to      The source at this call's end.
    /// @param in      frames samples.
    /// @param out     The layout's channel count of channels, frames each,
    ///                added to.
    /// @param frames  The frames.
    /// @return `maud_success`, or `maud_errorInvalid` for a NULL pointer or
    ///         a value that is not finite; nothing is written then.
    /// @par Thread safety
    /// Safe from any thread; the output is used by one thread at a time.
    MAUD_NODISCARD MAUD_API maudResult maudPanToSpeakers(const maudSpeakerPanner* panner,
                                                         const maudPanSource* from,
                                                         const maudPanSource* to, const float* in,
                                                         float* const* out, uint32_t frames);

#ifdef __cplusplus
}
#endif

#endif // MAUL_AUDIO_SPEAKERS_H
