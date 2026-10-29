// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Binaural effects: one per source, rendering its mono signal for both
// ears through a loaded HRTF set (maul-audio/hrtf.h). Processing runs on
// whatever thread the host renders on: it allocates nothing, takes no
// lock and does work in proportion to its frames.

#ifndef MAUL_AUDIO_BINAURAL_H
#define MAUL_AUDIO_BINAURAL_H

#include "maul-audio/base.h"
#include "maul-audio/hrtf.h"

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    // A source's binaural effect.
    typedef struct maudBinaural maudBinaural;

    // How to create a binaural effect. Build it with maudDefaultBinauralDef.
    typedef struct maudBinauralDef
    {
        uint32_t cookie;
        // MAUD_ABI_VERSION of the headers the program was built with; the
        // cookie and this stay the first two fields in every version.
        uint32_t version;
        // The set the effect renders through, at the rate the effect runs
        // at. It must outlive the effect.
        const maudHrtf* hrtf;
        // The most frames one call processes, 1 to
        // MAUD_MAX_BINAURAL_FRAMES.
        uint32_t maxFrames;
        // Whether the source's distance counts: each ear filtered for the
        // head's near field (a rigid sphere of headRadius, against the
        // distance the set was measured at) and looking its response up
        // in the direction it sees the source from (parallax).
        bool enableNearField;
        // The head's radius in metres, 0.05 to 0.15.
        float headRadius;
        maudAllocator allocator;
    } maudBinauralDef;

    // A source's parameters for one call.
    typedef struct maudBinauralParams
    {
        // Where the source is, from the head's centre, in metres in the
        // listener's frame. Its direction picks the responses; with the
        // near field, its length counts too, no nearer than 0.1 m. A zero
        // vector is straight ahead.
        maudVector3 position;
        // A gain the host computed, distance attenuation included: the
        // effect applies none of its own. It moves linearly from the
        // previous call's across the call.
        float gain;
    } maudBinauralParams;

// The cookie of a binaural def, which its default sets and
// maudCreateBinaural checks.
#define MAUD_BINAURAL_DEF_COOKIE 0x6D616262u

    /// Returns the default binaural def: no set, at most 1,024 frames per
    /// call, the near field on for a head of 8.75 cm.
    ///
    /// The default is built in the program from the headers it includes,
    /// so the version it stamps is the program's.
    ///
    /// @return The def, with a valid cookie and version.
    /// @par Thread safety
    /// Safe from any thread.
    static inline maudBinauralDef maudDefaultBinauralDef(void)
    {
#ifdef __cplusplus
        maudBinauralDef def = {};
#else
    maudBinauralDef def = {0};
#endif
        def.cookie = MAUD_BINAURAL_DEF_COOKIE;
        def.version = MAUD_ABI_VERSION;
        def.maxFrames = 1024;
        def.enableNearField = true;
        def.headRadius = 0.0875f;
        return def;
    }

    /// Creates a binaural effect, its memory taken once from the def's
    /// allocator.
    ///
    /// @param def      The def, from maudDefaultBinauralDef, with a set.
    /// @param effectOut  Receives the effect; NULL on failure.
    /// @return `maud_success`; `maud_errorVersion` for a def stamped with
    ///         another binary interface than the library's, checked
    ///         before anything else of it; `maud_errorLimit` for a
    ///         maxFrames past MAUD_MAX_BINAURAL_FRAMES; `maud_errorInvalid` for a NULL pointer or a
    ///         def without its cookie, without a set or out of range;
    ///         `maud_errorCapacity` when the allocator fails.
    /// @par Thread safety
    /// Safe from any thread.
    MAUD_NODISCARD MAUD_API maudResult maudCreateBinaural(const maudBinauralDef* def,
                                                          maudBinaural** effectOut);

    /// Destroys a binaural effect. NULL is ignored.
    ///
    /// @param effect  The effect.
    /// @par Thread safety
    /// Safe from any thread; the effect is used by one thread at a time.
    MAUD_API void maudDestroyBinaural(maudBinaural* effect);

    /// Renders frames of a source's mono signal for both ears, written to
    /// samplesOut[0] (left) and samplesOut[1] (right). When the position changes, the
    /// old and new responses are crossfaded while each ear's delay and
    /// near-field filter move, over 2.67 ms; a change during a fade
    /// starts when it ends, the latest one winning. The first call after
    /// creation or a reset starts at its parameters.
    ///
    /// @param effect      The effect.
    /// @param params      The source's parameters.
    /// @param samples     frameCount samples.
    /// @param samplesOut  Two channels of frameCount samples each, apart from samples.
    /// @param frameCount  0 to the def's maxFrames; 0 does nothing.
    /// @return `maud_success`; `maud_errorInvalid` for a NULL pointer or a
    ///         parameter that is not finite; `maud_errorCapacity` for more
    ///         frames than the def's maxFrames. On failure each channel of
    ///         samplesOut that is not NULL is silenced over frameCount samples,
    ///         but never past the def's maxFrames (MAUD_MAX_BINAURAL_FRAMES
    ///         for a NULL effect): a failed call writes no more than a
    ///         successful one could.
    /// @par Thread safety
    /// Safe from any thread; the effect is used by one thread at a time.
    MAUD_NODISCARD MAUD_API maudResult maudBinaural_Process(maudBinaural* effect,
                                                            const maudBinauralParams* params,
                                                            const float* samples,
                                                            float* const samplesOut[2],
                                                            uint32_t frameCount);

    /// Forgets the signal an effect has heard and its parameters, as for a
    /// source that starts again.
    ///
    /// @param effect  The effect.
    /// @return `maud_success`, or `maud_errorInvalid` for a NULL pointer.
    /// @par Thread safety
    /// Safe from any thread; the effect is used by one thread at a time.
    MAUD_NODISCARD MAUD_API maudResult maudBinaural_Reset(maudBinaural* effect);

#ifdef __cplusplus
}
#endif

#endif // MAUL_AUDIO_BINAURAL_H
