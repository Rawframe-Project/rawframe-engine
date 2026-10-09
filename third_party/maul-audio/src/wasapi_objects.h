// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Object streams on Windows: a spatial audio object render stream on the
// endpoint, its bed one static object per channel of the stream's
// layout, each of the stream's objects a dynamic object while it is
// active and the platform has one to give. The user's spatial format
// renders them; with none on, the bed plays and no object is available.

#ifndef MAUL_AUDIO_SRC_WASAPI_OBJECTS_H
#define MAUL_AUDIO_SRC_WASAPI_OBJECTS_H

#include "context_core.h"

// The COM interfaces' C macros, as the notifier takes them.
#ifndef COBJMACROS
#define COBJMACROS
#endif
#ifndef CONST_VTABLE
#define CONST_VTABLE
#endif
#include <mmdeviceapi.h>
#include <spatialaudioclient.h>

typedef struct maudWasapiObjects
{
    ISpatialAudioClient* client;
    ISpatialAudioObjectRenderStream* stream;
    // One block from the context: the bed's static objects (one per
    // channel), the dynamic objects held (NULL where not), the records
    // the period fills, and the frames (the bed interleaved, then each
    // object's), a pass's worth each.
    void* storage;
    size_t storageBytes;
    ISpatialAudioObject** bed;
    ISpatialAudioObject** held;
    maudStreamObject* records;
    float* frames;
    UINT32 passFrames;
    // The bed's channels and the stream's objects the block holds.
    uint32_t channels;
    uint32_t count;
} maudWasapiObjects;

// Activates an object render stream on device for an object stream,
// signalling event each pass, and its bed. maud_errorUnsupported where
// the endpoint has no spatial client, takes another rate or no bed of
// the stream's layout; maud_errorPlatform or maud_errorCapacity
// otherwise; nothing is left to close on failure.
maudResult maudWasapiOpenObjects(maudContext* context, IMMDevice* device, maudStreamCore* core,
                                 HANDLE event, maudWasapiObjects* objects);

// Releases every object, the stream and the client, and gives the block
// back.
void maudWasapiCloseObjects(maudContext* context, maudWasapiObjects* objects);

// One pass on the stream's thread: pulls the pass's frames of the bed
// and objects from the period (silence where running is false), writes
// the bed's channels and each active object's frames, placement and
// volume, and gives back the objects no longer active. Real-time safe.
// The pass's frame count goes to framesOut.
HRESULT maudWasapiRenderObjects(maudWasapiObjects* objects, maudStreamCore* core, bool running,
                                UINT32* framesOut);

#endif // MAUL_AUDIO_SRC_WASAPI_OBJECTS_H
