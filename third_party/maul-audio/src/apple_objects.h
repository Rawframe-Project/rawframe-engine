// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Object streams on macOS and iOS: Apple's spatial mixer in front of the
// stream's output unit. Each object is a point source on its own input
// bus, the bed passes through on the last; the mixer renders with the
// system's renderer for the output's kind (headphones, built-in
// speakers, other speakers), the listener's personalized HRTF where the
// system has one. The mixer applies no distance attenuation: the host's
// direct path already did.

#ifndef MAUL_AUDIO_SRC_APPLE_OBJECTS_H
#define MAUL_AUDIO_SRC_APPLE_OBJECTS_H

#include "context_core.h"

#include <AudioToolbox/AudioToolbox.h>

// The most frames one render brings.
#define MAUD_APPLE_OBJECT_SLICE 4096u

// What an input bus's render callback reads: the mixer's staging and the
// bus's place in it.
typedef struct maudAppleObjectBus
{
    const struct maudAppleObjects* objects;
    uint32_t index;
} maudAppleObjectBus;

typedef struct maudAppleObjects
{
    AudioUnit mixer;
    maudStreamCore* core;
    // One block from the context: the records the period fills, the
    // buses, the list the mixer renders into, the bed's frames
    // (interleaved), each object's frames, and the mixer's output (one
    // slice per channel, as the mixer takes no interleaved frames).
    void* storage;
    size_t storageBytes;
    maudStreamObject* records;
    maudAppleObjectBus* buses;
    AudioBufferList* mixedList;
    float* bed;
    float* mixed;
    // The frames of the render in progress.
    UInt32 frames;
} maudAppleObjects;

// Whether an object stream can render through the mixer: a mono or
// stereo bed, as the mixer's output.
bool maudAppleObjectsFit(const maudStreamCore* core);

// Makes and initializes the mixer for an object stream whose device
// leads to form. maud_errorPlatform or maud_errorCapacity on failure,
// with nothing left to close.
maudResult maudOpenAppleObjects(maudContext* context, maudStreamCore* core, maudDeviceForm form,
                                maudAppleObjects* objects);

// Disposes of the mixer and gives the block back.
void maudCloseAppleObjects(maudContext* context, maudAppleObjects* objects);

// On the IO thread: pulls frames of the stream's bed and objects, places
// each object's bus, and renders the mixer into out (frames of the
// stream's interleaved layout). Real-time safe.
OSStatus maudRenderAppleObjects(maudAppleObjects* objects, AudioUnitRenderActionFlags* flags,
                                const AudioTimeStamp* time, UInt32 frames, AudioBufferList* out);

#endif // MAUL_AUDIO_SRC_APPLE_OBJECTS_H
