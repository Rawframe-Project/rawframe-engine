// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The WASAPI backend's state.

#ifndef MAUL_AUDIO_SRC_WASAPI_CORE_H
#define MAUL_AUDIO_SRC_WASAPI_CORE_H

#include "context_core.h"
#include "device.h"
#include "sample_convert.h"
#include "wasapi_apartment.h"
#include "wasapi_notify.h"
#include "wasapi_objects.h"
#include "worker.h"

#include <audioclient.h>

// Bytes of an endpoint's ID and friendly name, in UTF-8.
#define MAUD_WASAPI_KEY_BYTES  128
#define MAUD_WASAPI_NAME_BYTES 256

typedef struct maudWasapiEndpoint
{
    char key[MAUD_WASAPI_KEY_BYTES];
    char name[MAUD_WASAPI_NAME_BYTES];
} maudWasapiEndpoint;

// A stream's audio client, run by its thread while the stream runs and
// by the control thread otherwise, never both at once.
typedef struct maudWasapiStream
{
    maudStreamCore* core;
    // The device the client is open on.
    maudDeviceId device;
    IAudioClient* client;
    IAudioRenderClient* render;
    IAudioCaptureClient* capture;
    // The device's playback position, for the clock, and its units per
    // second; the frames written (or, for an input, drained) since the
    // client started.
    IAudioClock* clock;
    UINT64 clockFrequency;
    uint64_t written;
    // Set by WASAPI each period, and by the control thread to stop.
    HANDLE bufferEvent;
    HANDLE stopEvent;
    uint32_t bufferFrames;
    // A buffer's worth of silence, which a silent capture packet passes
    // on.
    float* zeros;
    size_t zeroBytes;
    // Whether the client is exclusive, the device's sample format, and,
    // for an integer format, a buffer of floats the stream renders into
    // or reads from.
    bool exclusive;
    maudSampleKind sampleKind;
    float* scratch;
    size_t scratchBytes;
    maudWorker worker;
    bool threadRunning;
    // The thread ended on a failure, as when the endpoint went away.
    atomic_bool failed;
    // An object stream's spatial render stream, which it has in place of
    // the client; its stream is NULL for other streams.
    maudWasapiObjects objects;
} maudWasapiStream;

typedef struct maudWasapi
{
    maudContext* context;
    IMMDeviceEnumerator* enumerator;
    maudWasapiNotifier notifier;
    bool registered;
    // The thread every COM object of the backend's lives and is called
    // on.
    maudWasapiApartment apartment;
    // Room for a scan of as many endpoints as the context has devices.
    maudWasapiEndpoint* endpoints;
    maudDeviceSpec* specs;
    maudWasapiStream* streams;
    size_t bytes;
} maudWasapi;

#endif // MAUL_AUDIO_SRC_WASAPI_CORE_H
