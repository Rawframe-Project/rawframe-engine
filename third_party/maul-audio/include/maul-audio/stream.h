// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Streams: one direction of audio between a host and a device, in
// interleaved 32-bit float frames, handed to the host's real-time
// callback one fixed period at a time.

#ifndef MAUL_AUDIO_STREAM_H
#define MAUL_AUDIO_STREAM_H

#include "maul-audio/device.h"

#ifdef __cplusplus
extern "C"
{
#endif

    // Names a stream of a context: a 1-based slot, 0 for the null id, and
    // a generation that tells a live stream from earlier occupants of its
    // slot.
    typedef struct maudStreamId
    {
        uint32_t index1;
        uint32_t generation;
    } maudStreamId;

    // Which thread runs the stream's period loop.
    typedef uint8_t maudStreamMode;

    enum
    {
        // The platform's audio thread, or one the library starts where the
        // platform has none. On the web it is the page's main thread: the
        // library renders ahead there, from the browser's event loop, and
        // plays from a queue on the audio thread, a SharedArrayBuffer ring
        // on a cross-origin isolated page and posted chunks elsewhere
        // (docs/guide.md, "In a browser", gives the latency of each).
        maud_modeCallback = 0,
        // The host's own thread, through the library; the only mode of the
        // offline backend, and refused where the platform owns the audio
        // thread (PipeWire).
        maud_modePull = 1,
    };

    // How a stream's rate is chosen.
    typedef uint8_t maudRatePolicy;

    enum
    {
        // The device's own rate. The def's sampleRate must be 0.
        maud_rateNative = 0,
        // The def's sampleRate, which the device must run at without
        // conversion, or the stream is refused. On PipeWire that is the
        // graph's rate, which every device runs at.
        maud_rateRequired = 1,
        // The def's sampleRate, converted by the platform's own converter.
        // Refused where the platform has none.
        maud_ratePlatformConverted = 2,
    };

    // One object of an object stream (maul-audio/objects.h).
    typedef struct maudStreamObject maudStreamObject;

    // One period of a stream, as its callback sees it.
    typedef struct maudStreamBlock
    {
        // Output and duplex streams: frameCount interleaved frames to fill,
        // cleared to silence before the call. NULL for input streams.
        float* output;
        // Input and duplex streams: frameCount interleaved frames captured,
        // a duplex stream's in step with its output. NULL for output
        // streams.
        const float* input;
        uint32_t frameCount;
        uint32_t sampleRate;
        maudChannelLayout layout;
        // The stream frame index of the block's first frame.
        uint64_t position;
        // Object streams: the def's objectCount objects to fill and place
        // (maul-audio/objects.h), and how many active ones the platform
        // takes this period; the bed is output. NULL and 0 otherwise.
        maudStreamObject* objects;
        uint32_t objectCount;
        uint32_t objectsAvailable;
    } maudStreamBlock;

    // The host's real-time callback. It must not allocate, lock, wait or
    // make any control call on the stream's context; control calls made
    // from it are refused with maud_errorState.
    typedef void (*maudStreamCallback)(const maudStreamBlock* block, void* user);

    // Parts of the platform's voice processing, as flags: asked for by an
    // input or duplex stream, and reported as active.
    typedef uint8_t maudVoiceProcessing;

    enum
    {
        maud_voiceNone = 0,
        // Removes what the speakers play from what the microphone hears.
        maud_voiceEchoCancellation = 1,
        maud_voiceNoiseSuppression = 2,
        // Levels the microphone's signal.
        maud_voiceGainControl = 4,
    };

    // Whether a stream shares its device with other streams and
    // applications.
    typedef uint8_t maudShareMode;

    enum
    {
        // Through the platform's mixer, with others: the default.
        maud_shareShared = 0,
        // The device for this stream alone: WASAPI's exclusive mode,
        // CoreAudio's hog mode, an ALSA hardware PCM.
        maud_shareExclusive = 1,
    };

    // What the platform does with an output stream marked as already
    // spatialized (contentSpatialized).
    typedef uint8_t maudSpatialMark;

    enum
    {
        // The stream is not marked.
        maud_markNone = 0,
        // The platform does not say whether it spatializes the stream.
        maud_markUnknown = 1,
        // The platform leaves the stream unprocessed: it took the mark, or
        // never spatializes such a stream.
        maud_markHonored = 2,
        // The platform may spatialize the stream anyway.
        maud_markIgnored = 3,
    };

    // How a stream is made. Build it with maudDefaultStreamDef.
    typedef struct maudStreamDef
    {
        uint32_t cookie;
        maudDirection direction;
        maudStreamMode mode;
        maudRatePolicy ratePolicy;
        maudChannelLayout layout;
        // Frames per second for maud_rateRequired and
        // maud_ratePlatformConverted, from 8,000 to 384,000; 0 for
        // maud_rateNative.
        uint32_t sampleRate;
        // Frames per callback; 0 asks for the backend's default, 10 ms on
        // the offline backend.
        uint32_t periodFrames;
        // The device, or the null id to follow the default device of the
        // stream's direction and role. For a duplex stream, the output.
        maudDeviceId device;
        // A duplex stream's input device, or the null id to follow the
        // default input of its role. Unused by other streams.
        maudDeviceId inputDevice;
        // The platform voice processing an input or duplex stream asks for;
        // maud_voiceNone, the default, asks the platform to leave the
        // signal alone where it can. Output streams take maud_voiceNone.
        maudVoiceProcessing voice;
        // maud_shareExclusive asks for the device for this stream alone,
        // never falling back to shared: it needs a device (not the null
        // id), and is refused with maud_errorUnsupported where the
        // backend or device cannot give it (PipeWire, PulseAudio, the web,
        // the offline backend, ALSA's default PCM, duplex streams) and
        // with maud_errorPlatform while another application holds the
        // device. An exclusive stream plays the device's own rate:
        // maud_ratePlatformConverted is refused.
        maudShareMode share;
        // The role whose default a stream on the null device follows.
        maudDeviceRole role;
        // Whether an output or duplex stream's content is already
        // spatialized (a binaural or transaural mix), so that no platform
        // spatializer processes it again; the status says what the
        // platform did with the mark. Input streams take false.
        bool contentSpatialized;
        // An output stream's positioned objects, up to
        // MAUD_MAX_STREAM_OBJECTS: an object stream (maul-audio/objects.h),
        // whose layout is its bed's. Shared only; refused with
        // maud_errorUnsupported where the backend has no object renderer.
        // 0 for an ordinary stream.
        uint32_t objectCount;
        maudStreamCallback callback;
        void* user;
    } maudStreamDef;

    // What a stream runs at.
    typedef struct maudStreamFormat
    {
        uint32_t sampleRate;
        uint32_t periodFrames;
        maudChannelLayout layout;
        // The policy in effect.
        maudRatePolicy ratePolicy;
    } maudStreamFormat;

    // Why a stream cannot run.
    typedef uint8_t maudSuspendReason;

    enum
    {
        // It is not suspended.
        maud_suspendNone = 0,
        // Its device disappeared, and it was opened on that device.
        maud_suspendDeviceLost = 1,
        // It follows the default device, and its direction has no device.
        maud_suspendNoDevice = 2,
        // The platform holds audio until the user acts: on the web, until
        // a user gesture's handler calls maudResumeContext; on iOS, while
        // an interruption (a call, an alarm) lasts, and after one that
        // ended without the hint to resume, until maudResumeContext or a
        // focus request.
        maud_suspendPolicy = 3,
        // The platform has not granted access to its device: on the web,
        // until the user allows the microphone. A refusal leaves it here.
        maud_suspendPermission = 4,
        // The host suspended the context (maudSetContextSuspended): its
        // application is in the background, hidden, or asleep.
        maud_suspendHost = 5,
    };

    // Where a stream stands.
    // How a duplex stream keeps its input in step with its output.
    typedef uint8_t maudDriftPolicy;

    enum
    {
        // One direction, or both on one clock: nothing drifts. The ring
        // stays, absorbing the order of the halves' callbacks; the
        // platform absorbs any difference between the devices (PipeWire's
        // graph, the web's AudioContext, one CoreAudio device).
        maud_driftNone = 0,
        // Two clocks: the input waits in a ring held near two periods.
        // When it runs short the missing frames are silence; past four
        // periods the oldest beyond two are dropped. Each is counted.
        maud_driftSlip = 1,
    };

    typedef struct maudStreamStatus
    {
        // Whether the host started it.
        bool started;
        // Why it cannot run, or maud_suspendNone. A started stream that is
        // suspended renders nothing until it resumes.
        maudSuspendReason suspension;
        // The device it is on; the null id while it has none. For a duplex
        // stream, the output's.
        maudDeviceId device;
        // How a duplex stream's input follows its output.
        maudDriftPolicy drift;
        // Input frames a duplex stream has slipped: dropped, or played as
        // silence, under maud_driftSlip.
        uint64_t slippedFrames;
        // Whether the platform has said which voice processing runs on an
        // input or duplex stream; false while it has not, or where it
        // never says. Platforms may ignore a request.
        bool voiceReported;
        // The parts it said are active, once reported.
        maudVoiceProcessing voiceActive;
        // Times the platform revealed that an output played without the
        // stream's frames (underruns) or that an input lost frames before
        // the stream got them (overruns). A duplex stream's are its
        // output's and its input's; its slips are counted apart.
        uint64_t underruns;
        uint64_t overruns;
        // Whether the stream keeps other streams and applications off its
        // device: an exclusive stream, or an ALSA hardware PCM, which does
        // so even when opened shared.
        bool exclusive;
        // For a stream marked contentSpatialized, what the platform does
        // with the mark; maud_markNone for an unmarked stream.
        maudSpatialMark spatialMark;
    } maudStreamStatus;

    /// Returns the default stream def: an output stream in callback mode,
    /// stereo, at the device's native rate, with the backend's default
    /// period, following the general role's default device, and no
    /// callback.
    ///
    /// @return The def, with a valid cookie.
    /// @par Thread safety
    /// Safe from any thread.
    MAUD_API maudStreamDef maudDefaultStreamDef(void);

    /// Creates a stream, stopped, and allocates everything it will use.
    ///
    /// @param context      The context.
    /// @param def          The def, from maudDefaultStreamDef, with a
    ///                     callback.
    /// @param streamIdOut  Receives the stream's id; the null id on failure.
    /// @return `maud_success`; `maud_errorInvalid` for a NULL argument, a def
    ///         without its cookie or callback, or a value out of range;
    ///         `maud_errorUnsupported` for what the backend cannot do (the
    ///         offline backend has only pull mode and no converter);
    ///         `maud_errorCapacity` past the stream or period limit or when
    ///         the allocator fails; `maud_errorState` on a thread rendering
    ///         one of the context's streams.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MAUD_NODISCARD MAUD_API maudResult maudCreateStream(maudContext* context,
                                                        const maudStreamDef* def,
                                                        maudStreamId* streamIdOut);

    /// Destroys a stream. Its id becomes stale. On a platform backend its
    /// callback has returned for the last time when this returns.
    ///
    /// @param context  The context.
    /// @param stream   The stream.
    /// @return `maud_success`; `maud_errorStale` for an id that names no
    ///         stream; `maud_errorInvalid` for a NULL context;
    ///         `maud_errorState` on a thread rendering one of the context's
    ///         streams, or, on the offline backend, while another thread
    ///         renders the stream.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MAUD_NODISCARD MAUD_API maudResult maudDestroyStream(maudContext* context, maudStreamId stream);

    /// Starts a stream. Starting a started stream does nothing.
    ///
    /// @param context  The context.
    /// @param stream   The stream.
    /// @return `maud_success`; `maud_errorStale`; `maud_errorInvalid` for a
    ///         NULL context; `maud_errorState` on a thread rendering one of
    ///         the context's streams.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MAUD_NODISCARD MAUD_API maudResult maudStartStream(maudContext* context, maudStreamId stream);

    /// Stops a stream. Stopping a stopped stream does nothing.
    ///
    /// @param context  The context.
    /// @param stream   The stream.
    /// @return `maud_success`; `maud_errorStale`; `maud_errorInvalid` for a
    ///         NULL context; `maud_errorState` on a thread rendering one of
    ///         the context's streams.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MAUD_NODISCARD MAUD_API maudResult maudStopStream(maudContext* context, maudStreamId stream);

    /// Reports what a stream runs at.
    ///
    /// @param context    The context.
    /// @param stream     The stream.
    /// @param formatOut  Receives the format.
    /// @return `maud_success`; `maud_errorStale`; `maud_errorInvalid` for a
    ///         NULL pointer.
    /// @par Thread safety
    /// Safe from any thread.
    MAUD_NODISCARD MAUD_API maudResult maudGetStreamFormat(const maudContext* context,
                                                           maudStreamId stream,
                                                           maudStreamFormat* formatOut);

    /// Reports whether a stream is started, suspended, and on which device.
    ///
    /// @param context    The context.
    /// @param stream     The stream.
    /// @param statusOut  Receives the status.
    /// @return `maud_success`; `maud_errorStale`; `maud_errorInvalid` for a
    ///         NULL pointer.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MAUD_NODISCARD MAUD_API maudResult maudGetStreamStatus(const maudContext* context,
                                                           maudStreamId stream,
                                                           maudStreamStatus* statusOut);

    /// Reports how many frames a stream has moved to or from its device.
    ///
    /// @param context    The context.
    /// @param stream     The stream.
    /// @param framesOut  Receives the frame count.
    /// @return `maud_success`; `maud_errorStale`; `maud_errorInvalid` for a
    ///         NULL pointer.
    /// @par Thread safety
    /// Safe from any thread.
    MAUD_NODISCARD MAUD_API maudResult maudGetStreamPosition(const maudContext* context,
                                                             maudStreamId stream,
                                                             uint64_t* framesOut);

    // Where a stream's frames meet the host clock, as the platform last
    // reported it.
    typedef struct maudStreamClock
    {
        // A frame of the stream, counted from 0 as maudGetStreamPosition
        // counts.
        uint64_t position;
        // When that frame is heard (output) or was captured (input), in
        // nanoseconds of maudGetHostNanoseconds' clock; 0 before the
        // stream's first callback, and always on the offline backend,
        // whose clock is the caller's.
        int64_t hostNanoseconds;
        // How far that time is from the callback that produced or
        // received the frame: the buffers ahead of the device and the
        // platform's pipeline to it, with what the route adds.
        int64_t latencyNanoseconds;
    } maudStreamClock;

    /// Reports where a stream's frames meet the host clock. The platform
    /// stamps it at each callback, so it follows route changes; a frame's
    /// time follows from it at the stream's rate.
    ///
    /// @param context   The context.
    /// @param stream    The stream.
    /// @param clockOut  Receives the clock.
    /// @return `maud_success`; `maud_errorStale`; `maud_errorInvalid` for a
    ///         NULL pointer.
    /// @par Thread safety
    /// Safe from any thread. It never makes the rendering thread wait.
    MAUD_NODISCARD MAUD_API maudResult maudGetStreamClock(const maudContext* context,
                                                          maudStreamId stream,
                                                          maudStreamClock* clockOut);

    /// Returns the host clock streams are stamped with: CLOCK_MONOTONIC on
    /// Linux and other POSIX systems, the performance counter on Windows,
    /// the HAL's host time on macOS, `performance.now()` on the web.
    ///
    /// @return Nanoseconds since an unspecified start.
    /// @par Thread safety
    /// Real-time safe: no allocation, lock or wait.
    MAUD_API int64_t maudGetHostNanoseconds(void);

    /// Renders the next frames of an offline output stream into a caller
    /// buffer, calling the stream's callback once per period as needed, on
    /// the calling thread. The stream's clock advances by frameCount. A
    /// rate change from a move to another device applies from the first
    /// block this call produces.
    ///
    /// @param context     The context.
    /// @param stream      A started, running output stream of an offline
    ///                    context.
    /// @param framesOut   Room for frameCount interleaved frames. May be NULL
    ///                    when frameCount is 0.
    /// @param frameCount  The number of frames.
    /// @return `maud_success`; `maud_errorStale`; `maud_errorInvalid` for a
    ///         NULL pointer where frames are due, an input stream or a total
    ///         that does not fit in memory; `maud_errorUnsupported` on a
    ///         context that is not offline; `maud_errorState` for a stopped
    ///         or suspended stream or one already rendering.
    /// @par Thread safety
    /// Real-time safe: no allocation, lock or wait. A stream renders on one
    /// thread at a time.
    MAUD_NODISCARD MAUD_API maudResult maudRenderStream(maudContext* context, maudStreamId stream,
                                                        float* framesOut, uint32_t frameCount);

    /// Feeds the next frames to an offline input stream from a caller
    /// buffer, calling the stream's callback once per completed period, on
    /// the calling thread. The stream's clock advances by frameCount.
    ///
    /// @param context     The context.
    /// @param stream      A started, running input stream of an offline
    ///                    context.
    /// @param frames      frameCount interleaved frames. May be NULL when
    ///                    frameCount is 0.
    /// @param frameCount  The number of frames.
    /// @return `maud_success`; `maud_errorStale`; `maud_errorInvalid` for a
    ///         NULL pointer where frames are due, an output stream or a total
    ///         that does not fit in memory; `maud_errorUnsupported` on a
    ///         context that is not offline; `maud_errorState` for a stopped
    ///         or suspended stream or one already rendering.
    /// @par Thread safety
    /// Real-time safe: no allocation, lock or wait. A stream renders on one
    /// thread at a time.
    MAUD_NODISCARD MAUD_API maudResult maudFeedStream(maudContext* context, maudStreamId stream,
                                                      const float* frames, uint32_t frameCount);

#ifdef __cplusplus
}
#endif

#endif // MAUL_AUDIO_STREAM_H
