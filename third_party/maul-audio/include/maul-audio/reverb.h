// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The parametric reverb: a feedback delay network whose decay follows
// reverberation times given per band (up to 800 Hz, 800 Hz to 8 kHz,
// above 8 kHz). It takes the host's mono reverb send and adds a diffuse
// tail into a first-order ambisonic bed, which the binaural and speaker
// decoders render. Where a coupled space decays in two slopes, a reverb
// made with a tail renders the slower one too. One per listener; it
// allocates nothing once made.

#ifndef MAUL_AUDIO_REVERB_H
#define MAUL_AUDIO_REVERB_H

#include "maul-audio/base.h"
#include "maul-audio/direct.h"

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    // A reverb.
    typedef struct maudReverb maudReverb;

    // How to create a reverb. Build it with maudDefaultReverbDef.
    typedef struct maudReverbDef
    {
        uint32_t cookie;
        // The rate it runs at, 44,100 to 384,000 Hz.
        float sampleRate;
        // The longest input delay it will be asked for, 0 to 4 s.
        float maxDelay;
        // Whether it renders a slower second slope (the params' tail):
        // a second network, which doubles its memory and, while a tail
        // sounds, its work. False by default.
        bool tail;
        maudAllocator allocator;
    } maudReverbDef;

    // What the reverb does during one call.
    typedef struct maudReverbParams
    {
        // The time to decay by 60 dB in each band, 0.1 to 20 s, met at
        // the bands' geometric centres (126 Hz, 2.5 kHz, 12.6 kHz) and
        // between them smoothly.
        float reverbTime[MAUD_DIRECT_BANDS];
        // The send's level per band, -96 to 24 dB (0 leaves it as is):
        // a unit impulse sent gives W 10 ms bins of energy
        // 0.0144 exp(-13.8 t / T60) from 23 ms after the delay, times the
        // level's power.
        float level[MAUD_DIRECT_BANDS];
        // The send's delay, 0 to the def's maxDelay seconds.
        float delay;
        // The slower slope per band, as maudReverbResult's tailTime and
        // tailLevel give it, rendered by a reverb made with a tail (and
        // ignored by others): its time, 0.1 to 20 s, or 0 for none in the
        // band; and its level, as level is the first's, -96 to 24 dB. A
        // tail that stops decays at its time and then stops running.
        float tailTime[MAUD_DIRECT_BANDS];
        float tailLevel[MAUD_DIRECT_BANDS];
    } maudReverbParams;

    /// Returns the default reverb def: 48 kHz, no input delay.
    ///
    /// @return The def, with a valid cookie.
    /// @par Thread safety
    /// Safe from any thread.
    MAUD_API maudReverbDef maudDefaultReverbDef(void);

    /// Creates a reverb, silent.
    ///
    /// @param def        The def, from maudDefaultReverbDef.
    /// @param reverbOut  Receives the reverb; NULL on failure.
    /// @return `maud_success`; `maud_errorInvalid` for a NULL pointer, a
    ///         def without its cookie, a rate or longest delay out of
    ///         range;
    ///         `maud_errorCapacity` when the allocator fails.
    /// @par Thread safety
    /// Safe from any thread.
    MAUD_NODISCARD MAUD_API maudResult maudCreateReverb(const maudReverbDef* def,
                                                        maudReverb** reverbOut);

    /// Destroys a reverb. NULL is ignored.
    ///
    /// @param reverb  The reverb.
    /// @par Thread safety
    /// Safe from any thread; the reverb is used by one thread at a time.
    MAUD_API void maudDestroyReverb(maudReverb* reverb);

    /// Runs frames of the send through the reverb, adding its tail into a
    /// first-order bed (four channels, ACN order, SN3D), the send delayed
    /// and leveled per band first. When the times or levels change they
    /// move to the new ones across the call (a new delay takes effect at
    /// once); refitting the
    /// filters then takes some tens of microseconds on the calling
    /// thread, bounded and without allocation.
    ///
    /// @param reverb  The reverb.
    /// @param params  This call's params.
    /// @param in      frames samples of the send.
    /// @param bed     Four channels of frames samples, added to.
    /// @param frames  The frames; 0 does nothing.
    /// @return `maud_success`, or `maud_errorInvalid` for a NULL pointer or
    ///         a time, level or delay out of range or not finite (a tail's
    ///         too); nothing is written then.
    /// @par Thread safety
    /// Safe from any thread; the reverb is used by one thread at a time.
    MAUD_NODISCARD MAUD_API maudResult maudProcessReverb(maudReverb* reverb,
                                                         const maudReverbParams* params,
                                                         const float* in, float* const* bed,
                                                         uint32_t frames);

    /// Silences a reverb's tail.
    ///
    /// @param reverb  The reverb.
    /// @return `maud_success`, or `maud_errorInvalid` for a NULL pointer.
    /// @par Thread safety
    /// Safe from any thread; the reverb is used by one thread at a time.
    MAUD_NODISCARD MAUD_API maudResult maudResetReverb(maudReverb* reverb);

#ifdef __cplusplus
}
#endif

#endif // MAUL_AUDIO_REVERB_H
