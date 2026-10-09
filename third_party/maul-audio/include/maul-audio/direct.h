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

#define MAUD_DIRECT_BANDS 3

    // A source's direct effect.
    typedef struct maudDirectEffect maudDirectEffect;

    // How to create a direct effect. Build it with
    // maudDefaultDirectEffectDef.
    typedef struct maudDirectEffectDef
    {
        uint32_t cookie;
        // The rate the effect runs at, 32,000 to 384,000 Hz.
        float sampleRate;
        // Air absorption per band: the amplitude falls as
        // exp(-airAbsorption[band] * distance in metres).
        float airAbsorption[MAUD_DIRECT_BANDS];
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
        float transmission[MAUD_DIRECT_BANDS];
        // The source's directivity towards the listener, per band, 0 to
        // 1 (maudGetDirectivity computes it).
        float directivity[MAUD_DIRECT_BANDS];
    } maudDirectParams;

    // A directivity pattern: a weighted dipole per band, the gain
    // |(1 - weight) + weight cos(angle)|^power, where angle is between
    // the source's forward axis and the direction to the listener.
    typedef struct maudDirectivityPattern
    {
        // 0 omnidirectional, 0.5 cardioid, 1 dipole.
        float weight[MAUD_DIRECT_BANDS];
        // Sharpness; 1 for the plain pattern.
        float power[MAUD_DIRECT_BANDS];
    } maudDirectivityPattern;

    /// Returns the default direct effect def: 48 kHz, air absorption of
    /// ISO 9613 part 1 at 20 degrees C, 50 % humidity and 101.325 kPa.
    ///
    /// @return The def, with a valid cookie.
    /// @par Thread safety
    /// Safe from any thread.
    MAUD_API maudDirectEffectDef maudDefaultDirectEffectDef(void);

    /// Returns the default direct params: gain 1, no distance, a clear
    /// path, full transmission and directivity.
    ///
    /// @return The params.
    /// @par Thread safety
    /// Safe from any thread.
    MAUD_API maudDirectParams maudDefaultDirectParams(void);

    /// Computes air absorption per band from ISO 9613 part 1 at 101.325 kPa:
    /// the standard's attenuation averaged in dB over each band, as an
    /// amplitude exponent per metre.
    ///
    /// @param temperatureCelsius  -20 to 50.
    /// @param humidityPercent     Relative humidity, 10 to 100.
    /// @param absorptionOut       Receives three exponents.
    /// @return `maud_success`, or `maud_errorInvalid` for a NULL pointer or
    ///         a value out of range.
    /// @par Thread safety
    /// Safe from any thread.
    MAUD_NODISCARD MAUD_API maudResult maudGetAirAbsorption(float temperatureCelsius,
                                                            float humidityPercent,
                                                            float* absorptionOut);

    /// Computes a pattern's directivity towards the listener.
    ///
    /// @param pattern         The pattern.
    /// @param toListener      The direction from the source to the
    ///                        listener in the source's own frame, whose
    ///                        forward axis is -z; a zero vector counts as
    ///                        straight ahead.
    /// @param directivityOut  Receives three gains.
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
    /// @return `maud_success`; `maud_errorInvalid` for a NULL pointer, a
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
    /// @param effect  The effect.
    /// @param params  This call's params.
    /// @param in      frames samples.
    /// @param out     frames samples.
    /// @param frames  The frames; 0 does nothing.
    /// @return `maud_success`, or `maud_errorInvalid` for a NULL pointer, a
    ///         value that is not finite, a negative gain or distance, or an
    ///         occlusion, transmission or directivity outside 0 to 1;
    ///         nothing is written then.
    /// @par Thread safety
    /// Safe from any thread; the effect is used by one thread at a time.
    MAUD_NODISCARD MAUD_API maudResult maudProcessDirect(maudDirectEffect* effect,
                                                         const maudDirectParams* params,
                                                         const float* in, float* out,
                                                         uint32_t frames);

    /// Forgets the signal and the params an effect has seen.
    ///
    /// @param effect  The effect.
    /// @return `maud_success`, or `maud_errorInvalid` for a NULL pointer.
    /// @par Thread safety
    /// Safe from any thread; the effect is used by one thread at a time.
    MAUD_NODISCARD MAUD_API maudResult maudResetDirectEffect(maudDirectEffect* effect);

#ifdef __cplusplus
}
#endif

#endif // MAUL_AUDIO_DIRECT_H
