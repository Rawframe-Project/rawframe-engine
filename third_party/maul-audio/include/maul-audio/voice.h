// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// First-party voice processing: a voice activity detector and an
// automatic gain control. They stand apart from contexts and streams; a
// host runs them on captured frames, in its callback or anywhere else.

#ifndef MAUL_AUDIO_VOICE_H
#define MAUL_AUDIO_VOICE_H

#include "maul-audio/base.h"
#include "maul-audio/layout.h"

#include <stdbool.h>

#ifdef __cplusplus
extern "C"
{
#endif

    // A voice activity detector. Build its def with
    // maudDefaultVoiceDetectorDef.
    typedef struct maudVoiceDetectorDef
    {
        uint32_t cookie;
        // The frames' rate, from 8,000 to 384,000.
        uint32_t sampleRate;
        // The frames' layout; the detector listens to the channels' mean.
        maudChannelLayout layout;
        // How much evidence speech needs, from 0 to 3: a higher value
        // admits less noise and misses more quiet speech.
        uint8_t aggressiveness;
        // How long the detector stays active after speech ends.
        uint32_t hangoverMilliseconds;
        // Where its memory comes from; all zero for the library's default.
        maudAllocator allocator;
    } maudVoiceDetectorDef;

    // What a detector has concluded from the frames so far.
    typedef struct maudVoiceState
    {
        // Whether voice is active at the last whole 10 ms frame.
        bool active;
        // How likely that frame is speech, from 0 to 1.
        float probability;
        // That frame's level and the noise floor under it, in dBFS (a
        // full-scale sine is -3).
        float levelDbfs;
        float noiseDbfs;
        // The 10 ms frames analyzed so far.
        uint64_t frames;
    } maudVoiceState;

    typedef struct maudVoiceDetector maudVoiceDetector;

    /// Returns the default voice detector def: 48,000, mono,
    /// aggressiveness 1, a 200 ms hangover, the default allocator.
    ///
    /// @return The def, with a valid cookie.
    /// @par Thread safety
    /// Safe from any thread.
    MAUD_API maudVoiceDetectorDef maudDefaultVoiceDetectorDef(void);

    /// Creates a voice activity detector.
    ///
    /// @param def          The def, from maudDefaultVoiceDetectorDef.
    /// @param detectorOut  Receives the detector; NULL on failure.
    /// @return `maud_success`; `maud_errorInvalid` for a NULL pointer or a
    ///         def out of range; `maud_errorCapacity` when the allocator
    ///         fails.
    /// @par Thread safety
    /// Safe from any thread.
    MAUD_NODISCARD MAUD_API maudResult maudCreateVoiceDetector(const maudVoiceDetectorDef* def,
                                                               maudVoiceDetector** detectorOut);

    /// Destroys a voice activity detector. NULL is ignored.
    ///
    /// @param detector  The detector.
    /// @par Thread safety
    /// Safe from any thread; the detector is used by one thread at a time.
    MAUD_API void maudDestroyVoiceDetector(maudVoiceDetector* detector);

    /// Analyzes interleaved frames, in any count: what does not complete
    /// a 10 ms frame waits for the next call, so the results do not
    /// depend on how the frames are cut.
    ///
    /// @param detector    The detector.
    /// @param frames      frameCount frames in the def's layout; may be NULL
    ///                    when frameCount is 0.
    /// @param frameCount  How many.
    /// @param stateOut    Receives the state after them; may be NULL.
    /// @return `maud_success`; `maud_errorInvalid` for a NULL detector, or
    ///         NULL frames with a frameCount.
    /// @par Thread safety
    /// Real-time safe: no allocation, lock or wait. The detector is used
    /// by one thread at a time.
    MAUD_NODISCARD MAUD_API maudResult maudDetectVoice(maudVoiceDetector* detector,
                                                       const float* frames, uint32_t frameCount,
                                                       maudVoiceState* stateOut);

    // An automatic gain control. Build its def with
    // maudDefaultGainControlDef. It brings speech to a target level,
    // changing its gain slowly and only on evidence of speech, never
    // raising the noise above a ceiling, and limits peaks to -1 dBFS.
    typedef struct maudGainControlDef
    {
        uint32_t cookie;
        // The frames' rate, from 8,000 to 384,000, and their layout; one
        // gain applies to all channels.
        uint32_t sampleRate;
        maudChannelLayout layout;
        // The speech level it aims for, in dBFS, from -40 to -6.
        float targetDbfs;
        // The gain's range and its start, in dB: minGainDb from -30 to 0,
        // maxGainDb from 0 to 60, initialGainDb between them.
        float minGainDb;
        float maxGainDb;
        float initialGainDb;
        // The fastest the gain may change, from 1 to 30 dB per second.
        float maxChangeDbPerSecond;
        // The loudest the noise may become, in dBFS, from -80 to -20.
        float maxNoiseDbfs;
        // The aggressiveness of its voice detector, from 0 to 3.
        uint8_t aggressiveness;
        maudAllocator allocator;
    } maudGainControlDef;

    // What a gain control is doing.
    typedef struct maudGainState
    {
        // The gain applied at the end of the last frames, in dB.
        float gainDb;
        // The speech level it measured, in dBFS, once reliable.
        float speechDbfs;
        bool speechReliable;
        // The noise floor under the input, in dBFS.
        float noiseDbfs;
        // Whether its detector hears voice.
        bool voiceActive;
        // The 10 ms frames analyzed so far.
        uint64_t frames;
    } maudGainState;

    typedef struct maudGainControl maudGainControl;

    /// Returns the default gain control def: 48,000, mono, a target of
    /// -25 dBFS, gains from -10 to 50 dB starting at 15, 6 dB per second,
    /// noise kept under -50 dBFS, aggressiveness 1, the default allocator.
    ///
    /// @return The def, with a valid cookie.
    /// @par Thread safety
    /// Safe from any thread.
    MAUD_API maudGainControlDef maudDefaultGainControlDef(void);

    /// Creates an automatic gain control.
    ///
    /// @param def      The def, from maudDefaultGainControlDef.
    /// @param gainOut  Receives the gain control; NULL on failure.
    /// @return `maud_success`; `maud_errorInvalid` for a NULL pointer or a
    ///         def out of range; `maud_errorCapacity` when the allocator
    ///         fails.
    /// @par Thread safety
    /// Safe from any thread.
    MAUD_NODISCARD MAUD_API maudResult maudCreateGainControl(const maudGainControlDef* def,
                                                             maudGainControl** gainOut);

    /// Destroys an automatic gain control. NULL is ignored.
    ///
    /// @param gain  The gain control.
    /// @par Thread safety
    /// Safe from any thread; the gain control is used by one thread at a
    /// time.
    MAUD_API void maudDestroyGainControl(maudGainControl* gain);

    /// Applies the gain to interleaved frames in place, in any count; the
    /// output does not depend on how the frames are cut. The gain follows
    /// what the frames before them showed, so it adds no delay.
    ///
    /// @param gain        The gain control.
    /// @param frames      frameCount frames in the def's layout; may be NULL
    ///                    when frameCount is 0.
    /// @param frameCount  How many.
    /// @param stateOut    Receives the state after them; may be NULL.
    /// @return `maud_success`; `maud_errorInvalid` for a NULL gain control,
    ///         or NULL frames with a frameCount.
    /// @par Thread safety
    /// Real-time safe: no allocation, lock or wait. The gain control is
    /// used by one thread at a time.
    MAUD_NODISCARD MAUD_API maudResult maudApplyGainControl(maudGainControl* gain, float* frames,
                                                            uint32_t frameCount,
                                                            maudGainState* stateOut);

#ifdef __cplusplus
}
#endif

#endif // MAUL_AUDIO_VOICE_H
