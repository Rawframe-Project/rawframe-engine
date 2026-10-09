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

    // A noise suppressor. Build its def with
    // maudDefaultNoiseSuppressorDef. A high-pass filter takes out the
    // rumble below speech; then each 10 ms it estimates the noise's
    // spectrum, following it through speech, and takes each frequency
    // down by how likely it holds only noise, never below a floor. One
    // gain per frequency applies to all channels.
    typedef struct maudNoiseSuppressorDef
    {
        uint32_t cookie;
        // The frames' rate, from 8,000 to 384,000 and a multiple of 100,
        // and their layout.
        uint32_t sampleRate;
        maudChannelLayout layout;
        // The most it takes noise down, in dB, from -40 to -6.
        float floorDb;
        // The high-pass filter's corner, from 20 to 400 Hz, or 0 for none.
        float highPassHz;
        maudAllocator allocator;
    } maudNoiseSuppressorDef;

    // What a noise suppressor hears.
    typedef struct maudNoiseState
    {
        // The share of the last 10 ms's frequencies likely to hold speech,
        // 0 to 1.
        float speechProbability;
        // The noise it estimates under the input, in dBFS.
        float noiseDbfs;
        // The 10 ms frames analyzed so far.
        uint64_t frames;
    } maudNoiseState;

    typedef struct maudNoiseSuppressor maudNoiseSuppressor;

    /// Returns the default noise suppressor def: 48,000, mono, a floor of
    /// -30 dB, a high-pass at 100 Hz, the default allocator.
    ///
    /// @return The def, with a valid cookie.
    /// @par Thread safety
    /// Safe from any thread.
    MAUD_API maudNoiseSuppressorDef maudDefaultNoiseSuppressorDef(void);

    /// Creates a noise suppressor.
    ///
    /// @param def            The def, from maudDefaultNoiseSuppressorDef.
    /// @param suppressorOut  Receives the noise suppressor; NULL on failure.
    /// @return `maud_success`; `maud_errorInvalid` for a NULL pointer or a
    ///         def out of range; `maud_errorCapacity` when the allocator
    ///         fails.
    /// @par Thread safety
    /// Safe from any thread.
    MAUD_NODISCARD MAUD_API maudResult maudCreateNoiseSuppressor(
        const maudNoiseSuppressorDef* def, maudNoiseSuppressor** suppressorOut);

    /// Destroys a noise suppressor. NULL is ignored.
    ///
    /// @param suppressor  The noise suppressor.
    /// @par Thread safety
    /// Safe from any thread; the noise suppressor is used by one thread at
    /// a time.
    MAUD_API void maudDestroyNoiseSuppressor(maudNoiseSuppressor* suppressor);

    /// Suppresses noise in interleaved frames in place, in any count; the
    /// output does not depend on how the frames are cut. It lags the
    /// input by 10 ms: the first 10 ms out are silence.
    ///
    /// @param suppressor  The noise suppressor.
    /// @param frames      frameCount frames in the def's layout; may be
    ///                    NULL when frameCount is 0.
    /// @param frameCount  How many.
    /// @param stateOut    Receives the state after them; may be NULL.
    /// @return `maud_success`; `maud_errorInvalid` for a NULL noise
    ///         suppressor, or NULL frames with a frameCount.
    /// @par Thread safety
    /// Real-time safe: no allocation, lock or wait. The noise suppressor
    /// is used by one thread at a time.
    MAUD_NODISCARD MAUD_API maudResult maudSuppressNoise(maudNoiseSuppressor* suppressor,
                                                         float* frames, uint32_t frameCount,
                                                         maudNoiseState* stateOut);

    // An echo canceller: it takes out of the capture what the render
    // played into it, as a call's far end heard through the device's
    // loudspeaker, and the noise with it. Ahead of a multidelay block
    // frequency-domain adaptive filter, a DC notch on the capture and a
    // pre-emphasis on both; the filter's rates follow the echo's leakage
    // into its output, and it adapts in a background copy the output
    // takes over only when it does better. After it, one gain per
    // frequency against the noise and the echo the filter leaves, under
    // the probability of speech. A host runs it in place of the noise
    // suppressor, and gives it the render already aligned with the
    // capture (the stream's latencies), both mono at one rate.
    typedef struct maudEchoCancellerDef
    {
        uint32_t cookie;
        // The frames' rate, from 8,000 to 384,000.
        uint32_t sampleRate;
        // The longest echo path it learns, in seconds, from 0.05 to 1.
        float tailSeconds;
        // The most it takes noise down, in dB, from -40 to -6 (the echo
        // left, down to -40 dB where no one speaks).
        float floorDb;
        maudAllocator allocator;
    } maudEchoCancellerDef;

    // What an echo canceller has learnt.
    typedef struct maudEchoState
    {
        // The share of the echo it estimates that is still in its output,
        // 0.005 to 1: high while it learns or after the path changes.
        float leakage;
        // Whether it has learnt the echo path once.
        bool adapted;
        // The probability that the last frame held speech, 0.1 to 1.
        float speechProbability;
        // The frames processed so far.
        uint64_t frames;
    } maudEchoState;

    typedef struct maudEchoCanceller maudEchoCanceller;

    /// Returns the default echo canceller def: 48,000, a path of 0.2 s,
    /// a noise floor of -15 dB, the default allocator.
    ///
    /// @return The def, with a valid cookie.
    /// @par Thread safety
    /// Safe from any thread.
    MAUD_API maudEchoCancellerDef maudDefaultEchoCancellerDef(void);

    /// Creates an echo canceller.
    ///
    /// @param def            The def, from maudDefaultEchoCancellerDef.
    /// @param cancellerOut   Receives the echo canceller; NULL on failure.
    /// @return `maud_success`; `maud_errorInvalid` for a NULL pointer or a
    ///         def out of range; `maud_errorCapacity` when the allocator
    ///         fails.
    /// @par Thread safety
    /// Safe from any thread.
    MAUD_NODISCARD MAUD_API maudResult maudCreateEchoCanceller(const maudEchoCancellerDef* def,
                                                               maudEchoCanceller** cancellerOut);

    /// Destroys an echo canceller. NULL is ignored.
    ///
    /// @param canceller  The echo canceller.
    /// @par Thread safety
    /// Safe from any thread; the echo canceller is used by one thread at a
    /// time.
    MAUD_API void maudDestroyEchoCanceller(maudEchoCanceller* canceller);

    /// Takes the echo of the render out of the capture, in place, in any
    /// count of frames; the output does not depend on how the frames are
    /// cut. It works in blocks of the smallest power of two of frames
    /// lasting 8 ms or more (128 at 16,000, 512 at 48,000) and lags the
    /// input by two: the first two blocks out are silence.
    ///
    /// @param canceller   The echo canceller.
    /// @param capture     frameCount mono frames of the capture; may be NULL
    ///                    when frameCount is 0.
    /// @param render      frameCount mono frames of what was played, aligned
    ///                    with the capture; may be NULL when frameCount is
    ///                    0.
    /// @param frameCount  How many.
    /// @param stateOut    Receives the state after them; may be NULL.
    /// @return `maud_success`; `maud_errorInvalid` for a NULL echo
    ///         canceller, or NULL frames with a frameCount.
    /// @par Thread safety
    /// Real-time safe: no allocation, lock or wait. The echo canceller is
    /// used by one thread at a time.
    MAUD_NODISCARD MAUD_API maudResult maudCancelEcho(maudEchoCanceller* canceller, float* capture,
                                                      const float* render, uint32_t frameCount,
                                                      maudEchoState* stateOut);

#ifdef __cplusplus
}
#endif

#endif // MAUL_AUDIO_VOICE_H
