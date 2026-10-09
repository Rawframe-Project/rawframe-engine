// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The interface every backend implements. The context and stream
// modules validate what callers pass; a backend reports its devices and
// settles what a device can do, refusing the rest.

#ifndef MAUL_AUDIO_SRC_BACKEND_H
#define MAUL_AUDIO_SRC_BACKEND_H

#include "context_core.h"

struct maudBackend
{
    maudBackendKind kind;
    // Connects a new context to the platform and fills its device table.
    // maud_errorUnsupported when the platform's service is missing,
    // maud_errorCapacity when the context's limits cannot hold the
    // starting devices.
    maudResult (*openContext)(maudContext* context);
    // Releases what openContext set up. May be NULL.
    void (*closeContext)(maudContext* context);
    // Takes in the platform's pending reports, without blocking. May be
    // NULL.
    void (*pump)(maudContext* context);
    // Settles a stream's format from a def already checked for ranges, on
    // device (NULL while the stream has none), or refuses it:
    // maud_errorUnsupported for a mode, policy or format the backend
    // cannot run.
    maudResult (*openStream)(const maudContext* context, const maudStreamDef* def,
                             const maudDeviceInfo* device, maudStreamFormat* formatOut);
    // Connects a stream's platform side once its slot's core is set up.
    // May be NULL.
    maudResult (*attachStream)(maudContext* context, maudStreamSlot* slot);
    // Releases what attachStream set up; afterwards no platform thread
    // touches the core. May be NULL.
    void (*detachStream)(maudContext* context, maudStreamSlot* slot);
    // Lets the platform run the stream or holds it. May be NULL.
    void (*setStreamActive)(maudContext* context, maudStreamSlot* slot, bool active);
    // Offers the stream's format again after a move changed its rate, or,
    // with reopensOnMove, connects it again after any move. May be NULL.
    void (*retargetStream)(maudContext* context, maudStreamSlot* slot);
    // Whether the platform binds a stream to one device, so that every
    // move must reopen it; otherwise the platform moves it itself.
    bool reopensOnMove;
    // Whether a duplex stream's halves, on their current devices, run on
    // one clock. May be NULL: never.
    bool (*sharesClock)(const maudContext* context, const maudStreamSlot* output,
                        const maudStreamSlot* input);
    // Asks the platform to let a context held by a policy run. May be
    // NULL.
    void (*resumeContext)(maudContext* context);
    // Tells the platform the host suspended the context, or resumed it,
    // after its streams were suspended or before they resume. May be
    // NULL.
    void (*suspendContext)(maudContext* context, bool suspended);
    // Whether the caller's thread renders the streams (maudRenderStream
    // and maudFeedStream), as on the offline backend.
    bool rendersOnCaller;
    // Whether the backend renders object streams (maul-audio/objects.h):
    // hands their objects to a platform renderer, or to the caller.
    bool rendersObjects;
    // Whether the platform has no voice processing, so input streams
    // report none from the start; otherwise the backend reports.
    bool hasNoVoice;
    // Whether it can open a stream for exclusive use; its openStream
    // still refuses a device that cannot be.
    bool exclusive;
    // What a stream marked contentSpatialized gets from the platform,
    // from its def and settled format; a backend whose platform says only
    // once the stream is open reports again through the core's
    // spatialMark. May be NULL: unknown.
    maudSpatialMark (*markStream)(const maudStreamDef* def, const maudStreamFormat* format);
    // Asks the platform for audio focus or gives it back (maudRequestFocus,
    // whose results it returns); the backend reports the state that
    // follows through maudReportFocus. May be NULL: no audio focus.
    maudResult (*requestFocus)(maudContext* context, maudFocusRequest request, maudDeviceRole role);
};

// The offline backend.
const maudBackend* maudGetOfflineBackend(void);

// The PipeWire backend, in builds with MAUL_AUDIO_PIPEWIRE.
const maudBackend* maudGetPipewireBackend(void);

// The PulseAudio backend, in builds with MAUL_AUDIO_PULSE.
const maudBackend* maudGetPulseBackend(void);

// The ALSA backend, in builds with MAUL_AUDIO_ALSA.
const maudBackend* maudGetAlsaBackend(void);

// The WASAPI backend, in builds with MAUL_AUDIO_WASAPI.
const maudBackend* maudGetWasapiBackend(void);

// The CoreAudio backend, in builds with MAUL_AUDIO_COREAUDIO.
const maudBackend* maudGetCoreAudioBackend(void);

// The iOS backend, in builds with MAUL_AUDIO_IOS.
const maudBackend* maudGetIosBackend(void);

// The AAudio backend, in builds with MAUL_AUDIO_AAUDIO.
const maudBackend* maudGetAaudioBackend(void);

// The web backend, in Emscripten builds.
const maudBackend* maudGetWebBackend(void);

// The private backend, in builds with MAUL_AUDIO_PRIVATE_BACKEND: written
// outside the library (docs/private-backends.md) against this header.
const maudBackend* maudGetPrivateBackend(void);

#endif // MAUL_AUDIO_SRC_BACKEND_H
