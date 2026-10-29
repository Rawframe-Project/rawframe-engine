// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The direct effect: what the direct path does to a source before it is
// spatialized, in three bands (up to 800 Hz, 800 Hz to 8 kHz, above
// 8 kHz). Air absorption over distance, a directivity pattern, occlusion
// and transmission through what blocks the path; distance attenuation is
// the host's own curve, given as a gain. One effect per source, mono in
// and mono out, ahead of the binaural effect or a speaker panner.

#ifndef MAUL_AUDIO_DIRECT_H
#define MAUL_AUDIO_DIRECT_H

#include "maul-audio/base.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

// The bands of every frequency-dependent value of the Spatial part: air
// absorption, transmission, directivity, materials, reverberation times
// and levels, and a bake's energy fields. Low first: up to 800 Hz, 800 Hz
// to 8 kHz, above 8 kHz. It sizes public structs and the bake format, so
// from 1.0.0 it never changes.
#define MAUD_BANDS 3

    // A source's direct effect.
    typedef struct maudDirectEffect maudDirectEffect;

    // How to create a direct effect. Build it with
    // maudDefaultDirectEffectDef.
    typedef struct maudDirectEffectDef
    {
        uint32_t cookie;
        // MAUD_ABI_VERSION of the headers the program was built with; the
        // cookie and this stay the first two fields in every version.
        uint32_t version;
        // The rate the effect runs at, 32,000 to 384,000 Hz.
        uint32_t sampleRate;
        // Air absorption per band: the amplitude falls as
        // exp(-airAbsorption[band] * distance in metres).
        float airAbsorption[MAUD_BANDS];
        maudAllocator allocator;
    } maudDirectEffectDef;

    // What the direct path does to a source during one call.
    typedef struct maudDirectParams
    {
        // The host's gain: its distance attenuation and anything else.
        float gain;
        // The path's length in metres, for air absorption; 0 for none.
        float distance;
        // How much of the source the path's obstacles hide: 0 a clear
        // path, 1 fully blocked.
        float occlusion;
        // What passes through the obstacles, per band, 0 to 1.
        float transmission[MAUD_BANDS];
        // The source's directivity towards the listener, per band, 0 to
        // 1 (maudGetDirectivity computes it).
        float directivity[MAUD_BANDS];
    } maudDirectParams;

    // A directivity pattern: a weighted dipole per band, the gain
    // |(1 - weight) + weight cos(angle)|^power, where angle is between
    // the source's forward axis and the direction to the listener.
    typedef struct maudDirectivityPattern
    {
        // 0 omnidirectional, 0.5 cardioid, 1 dipole.
        float weight[MAUD_BANDS];
        // Sharpness; 1 for the plain pattern.
        float power[MAUD_BANDS];
    } maudDirectivityPattern;

    /// Returns the default direct params: gain 1, no distance, a clear
    /// path, full transmission and directivity.
    ///
    /// @return The params.
    /// @par Thread safety
    /// Safe from any thread.
    MAUD_API maudDirectParams maudDefaultDirectParams(void);

    /// Computes air absorption per band from ISO 9613 part 1 at 101.325 kPa:
    /// the standard's attenuation averaged in dB over each band, as an
    /// amplitude exponent per metre. The same bits on every platform and
    /// in any floating-point mode, since bakes take it as an input.
    ///
    /// @param temperatureCelsius  -20 to 50.
    /// @param humidityPercent     Relative humidity, 10 to 100.
    /// @param absorptionOut       Receives three exponents; left as they
    ///                            were on failure.
    /// @return `maud_success`, or `maud_errorInvalid` for a NULL pointer or
    ///         a value out of range.
    /// @par Thread safety
    /// Safe from any thread.
    MAUD_NODISCARD MAUD_API maudResult maudGetAirAbsorption(float temperatureCelsius,
                                                            float humidityPercent,
                                                            float* absorptionOut);

// The cookie of a direct effect def, which its default sets and
// maudCreateDirectEffect checks.
#define MAUD_DIRECT_EFFECT_DEF_COOKIE 0x6D616465u

    /// Returns the default direct effect def: 48 kHz, air absorption of
    /// ISO 9613 part 1 at 20 degrees C, 50 % humidity and 101.325 kPa.
    ///
    /// The default is built in the program from the headers it includes,
    /// so the version it stamps is the program's.
    ///
    /// @return The def, with a valid cookie and version.
    /// @par Thread safety
    /// Safe from any thread.
    static inline maudDirectEffectDef maudDefaultDirectEffectDef(void)
    {
#ifdef __cplusplus
        maudDirectEffectDef def = {};
#else
    maudDirectEffectDef def = {0};
#endif
        def.cookie = MAUD_DIRECT_EFFECT_DEF_COOKIE;
        def.version = MAUD_ABI_VERSION;
        def.sampleRate = 48000;
        (void)maudGetAirAbsorption(20.0f, 50.0f, def.airAbsorption);
        return def;
    }

    /// Computes a pattern's directivity towards the listener.
    ///
    /// @param pattern         The pattern.
    /// @param toListener      The direction from the source to the
    ///                        listener in the source's own frame, whose
    ///                        forward axis is -z; a zero vector counts as
    ///                        straight ahead.
    /// @param directivityOut  Receives three gains; left as they were on
    ///                        failure.
    /// @return `maud_success`, or `maud_errorInvalid` for a NULL pointer, a
    ///         weight outside 0 to 1, a power below 0 or a value that is
    ///         not finite.
    /// @par Thread safety
    /// Safe from any thread.
    MAUD_NODISCARD MAUD_API maudResult maudGetDirectivity(const maudDirectivityPattern* pattern,
                                                          maudVector3 toListener,
                                                          float* directivityOut);

    /// Creates a direct effect.
    ///
    /// @param def        The def, from maudDefaultDirectEffectDef.
    /// @param effectOut  Receives the effect; NULL on failure.
    /// @return `maud_success`; `maud_errorVersion` for a def stamped with
    ///         another binary interface than the library's, checked
    ///         before anything else of it; `maud_errorInvalid` for a NULL pointer, a
    ///         def without its cookie, a rate out of range or air
    ///         absorption below 0 or not finite; `maud_errorCapacity` when
    ///         the allocator fails.
    /// @par Thread safety
    /// Safe from any thread.
    MAUD_NODISCARD MAUD_API maudResult maudCreateDirectEffect(const maudDirectEffectDef* def,
                                                              maudDirectEffect** effectOut);

    /// Destroys a direct effect. NULL is ignored.
    ///
    /// @param effect  The effect.
    /// @par Thread safety
    /// Safe from any thread; the effect is used by one thread at a time.
    MAUD_API void maudDestroyDirectEffect(maudDirectEffect* effect);

    /// Processes frames, in place allowed. The gain and the band filters
    /// move from the previous call's params to these across the call; the
    /// first call after creation or a reset starts at these. Band shapes
    /// are refitted only when a band moves by 0.05 dB or more.
    ///
    /// @param effect      The effect.
    /// @param params      This call's params.
    /// @param samples     frameCount samples.
    /// @param samplesOut  frameCount samples; may be samples itself.
    /// @param frameCount  The frames; 0 does nothing.
    /// @return `maud_success`, or `maud_errorInvalid` for a NULL pointer, a
    ///         value that is not finite, a negative gain or distance, or an
    ///         occlusion, transmission or directivity outside 0 to 1. On
    ///         failure samplesOut, unless NULL, is silenced over frames
    ///         samples, and so is samples when it is samplesOut.
    /// @par Thread safety
    /// Safe from any thread; the effect is used by one thread at a time.
    MAUD_NODISCARD MAUD_API maudResult maudDirectEffect_Process(maudDirectEffect* effect,
                                                                const maudDirectParams* params,
                                                                const float* samples,
                                                                float* samplesOut,
                                                                uint32_t frameCount);

    /// Forgets the signal and the params an effect has seen.
    ///
    /// @param effect  The effect.
    /// @return `maud_success`, or `maud_errorInvalid` for a NULL pointer.
    /// @par Thread safety
    /// Safe from any thread; the effect is used by one thread at a time.
    MAUD_NODISCARD MAUD_API maudResult maudDirectEffect_Reset(maudDirectEffect* effect);

#ifdef __cplusplus
}
#endif

#endif // MAUL_AUDIO_DIRECT_H
