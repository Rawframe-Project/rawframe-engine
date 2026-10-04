// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The Device part's root: a context holds the connection to a backend
// and the streams opened through it.

#ifndef MAUL_AUDIO_CONTEXT_H
#define MAUL_AUDIO_CONTEXT_H

#include "maul-audio/base.h"

#include <stdbool.h>

#ifdef __cplusplus
extern "C"
{
#endif

    // A context. Create it with maudCreateContext.
    typedef struct maudContext maudContext;

    // Which backend a context talks to.
    typedef uint8_t maudBackendKind;

    enum
    {
        // The platform's best backend that answers: on Linux PipeWire, then
        // PulseAudio, then ALSA, which answers wherever libasound loads;
        // on Windows WASAPI; on macOS CoreAudio; on the web Web Audio. Only asked for; a context
        // reports the backend it chose.
        maud_backendNative = 0,
        // Rendering to caller buffers at a caller-driven clock: no device,
        // no thread, the same samples from the same inputs. Always built.
        maud_backendOffline = 1,
        maud_backendPipewire = 2,
        maud_backendPulse = 3,
        maud_backendAlsa = 4,
        maud_backendWasapi = 5,
        maud_backendCoreAudio = 6,
        maud_backendAaudio = 7,
        maud_backendWeb = 8,
    };

    // The named limits of a context. A request past one is refused.
    typedef struct maudLimits
    {
        // Streams that exist at once.
        uint16_t streams;
        // The largest period a stream may ask for, in frames.
        uint32_t periodFrames;
        // Devices, of both directions, that exist at once.
        uint16_t devices;
        // Records the notification queue holds before it overflows; at
        // least 2.
        uint16_t notifications;
        // Bytes of a device's name, and of its key.
        uint16_t deviceTextBytes;
    } maudLimits;

    // How a context is made. Build it with maudDefaultContextDef.
    typedef struct maudContextDef
    {
        uint32_t cookie;
        maudAllocator allocator;
        maudLimits limits;
        maudBackendKind backend;
        // The rate the offline backend's device runs at, in frames per
        // second, from 8,000 to 384,000.
        uint32_t offlineSampleRate;
    } maudContextDef;

    /// Returns the default context def: 8 streams, periods of at most
    /// 8,192 frames, 32 devices, 256 notifications, 256 bytes of device
    /// name and key, the C library's allocator, the native backend and an
    /// offline rate of 48,000.
    ///
    /// @return The def, with a valid cookie.
    /// @par Thread safety
    /// Safe from any thread.
    MAUD_API maudContextDef maudDefaultContextDef(void);

    /// Creates a context. An offline context starts with one output and one
    /// input device at the offline rate, stereo, each the default of its
    /// direction for every role. A native context on Linux connects to
    /// PipeWire, waiting up to two seconds on the calling thread for its
    /// device list. Either starts with an empty notification queue.
    ///
    /// @param def         The def, from maudDefaultContextDef.
    /// @param contextOut  Receives the context; set to NULL on failure.
    /// @return `maud_success`; `maud_errorInvalid` for a NULL argument, a def
    ///         without its cookie, an allocator with one function, a limit
    ///         of 0 or an offline rate out of range; `maud_errorUnsupported`
    ///         when this build or platform lacks the backend asked for, or
    ///         no audio service of it answers;
    ///         `maud_errorCapacity` when the allocator fails.
    /// @par Thread safety
    /// Safe from any thread.
    MAUD_NODISCARD MAUD_API maudResult maudCreateContext(const maudContextDef* def,
                                                         maudContext** contextOut);

    /// Destroys a context and every stream it still holds.
    ///
    /// @param context  The context. NULL does nothing.
    /// @return `maud_success`, or `maud_errorState` when called on a thread
    ///         that is rendering one of its streams; the context is then
    ///         left as it was and the call counts as misuse.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    /// No stream of the context may be rendering on another thread.
    MAUD_NODISCARD MAUD_API maudResult maudDestroyContext(maudContext* context);

    /// Asks the platform to let the context's audio run. Browsers hold audio
    /// until the user acts: call it from a user gesture's handler, such as
    /// a click's. Streams held meanwhile are suspended with
    /// maud_suspendPolicy and resume, with a notification, once the
    /// platform lets the context run. Where no policy holds audio it does
    /// nothing.
    ///
    /// @param context  The context.
    /// @return `maud_success`; `maud_errorInvalid` for a NULL context;
    ///         `maud_errorState` when called on a thread that is rendering
    ///         one of the context's streams, which counts as misuse.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MAUD_NODISCARD MAUD_API maudResult maudResumeContext(maudContext* context);

    /// Suspends a context for the host's lifecycle, or resumes it: the
    /// application went to the background, its tab was hidden or the
    /// system sleeps, and later it is back. The library never infers this
    /// itself. Suspending suspends every stream that runs or only waits
    /// to run with maud_suspendHost, with a notification each, and stops
    /// its platform side; on the web it also suspends the AudioContext.
    /// Resuming lets each run again, or wait for what it waited for. A
    /// stream that lost its device keeps that reason. Calling it again
    /// the same way does nothing.
    ///
    /// @param context    The context.
    /// @param suspended  Whether the host is suspended.
    /// @return `maud_success`; `maud_errorInvalid` for a NULL context;
    ///         `maud_errorState` when called on a thread that is rendering
    ///         one of the context's streams, which counts as misuse.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MAUD_NODISCARD MAUD_API maudResult maudSetContextSuspended(maudContext* context,
                                                               bool suspended);

    /// Returns the backend a context talks to: the one its def named, or
    /// for maud_backendNative the one it chose.
    ///
    /// @param context  The context.
    /// @return The backend kind, never maud_backendNative.
    /// @par Thread safety
    /// Safe from any thread.
    MAUD_API maudBackendKind maudGetContextBackend(const maudContext* context);

    /// Returns how many calls the context refused as misuse: invalid
    /// arguments against it, and control calls made on a thread that was
    /// rendering one of its streams. Stale ids are not misuse.
    ///
    /// @param context  The context.
    /// @return The count since the context was created.
    /// @par Thread safety
    /// Safe from any thread.
    MAUD_API uint64_t maudGetContextMisuse(const maudContext* context);

#ifdef __cplusplus
}
#endif

#endif // MAUL_AUDIO_CONTEXT_H
