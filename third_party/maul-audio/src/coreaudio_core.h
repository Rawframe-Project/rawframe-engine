// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The CoreAudio backend's state, shared by its context and stream
// modules.

#ifndef MAUL_AUDIO_SRC_COREAUDIO_CORE_H
#define MAUL_AUDIO_SRC_COREAUDIO_CORE_H

#include "apple_objects.h"
#include "context_core.h"
#include "device.h"

#include <AudioToolbox/AudioToolbox.h>
#include <CoreAudio/CoreAudio.h>
#include <dispatch/dispatch.h>
#include <stdatomic.h>

// Bytes kept for a device's UID and for its name, in UTF-8.
#define MAUD_COREAUDIO_KEY_BYTES  256u
#define MAUD_COREAUDIO_NAME_BYTES 256u
// Bytes for one property read of variable size: a device's stream
// configuration or its rate ranges.
#define MAUD_COREAUDIO_SCRATCH_BYTES 4096u

// The main element of every HAL property: kAudioObjectPropertyElementMain
// (macOS 12) by its value, so older deployment targets build.
#define MAUD_COREAUDIO_ELEMENT_MAIN 0u

// One scanned device direction's text, which its spec points into.
typedef struct maudCoreAudioEndpoint
{
    AudioObjectID object;
    char key[MAUD_COREAUDIO_KEY_BYTES];
    char name[MAUD_COREAUDIO_NAME_BYTES];
} maudCoreAudioEndpoint;

// One direction of a device whose data source the context listens to.
typedef struct maudCoreAudioWatch
{
    AudioObjectID object;
    maudDirection direction;
} maudCoreAudioWatch;

// A stream's AUHAL unit, run by the HAL's IO thread while it plays.
typedef struct maudCoreAudioStream
{
    maudStreamCore* core;
    AudioComponentInstance unit;
    bool playing;
    // What the device adds between a buffer's host time and the speaker,
    // or between the microphone and it: its latency, its safety offset
    // and its first stream's latency, in nanoseconds.
    int64_t deviceLatency;
    // An input stream's buffer list, with room for the most frames one
    // render brings, as allocated.
    AudioBufferList* captured;
    size_t capturedBytes;
    // The channels the unit captures when fewer than the stream's: the
    // voice-processing unit captures one, spread to every channel. 0 for
    // the stream's own.
    uint32_t unitChannels;
    // The device sample time the next IO cycle should start at, or a
    // negative value before the first: a later start skipped cycles.
    Float64 nextSampleTime;
    // A voiced duplex stream's halves share the input half's
    // voice-processing unit and point at each other; the output half has
    // no unit of its own.
    struct maudCoreAudioStream* voicePartner;
    // The device an exclusive stream holds in hog mode, or
    // kAudioObjectUnknown.
    AudioObjectID hogged;
    // An object stream's spatial mixer, which renders for the unit; its
    // mixer is NULL for other streams.
    maudAppleObjects objects;
} maudCoreAudioStream;

typedef struct maudCoreAudio
{
    maudContext* context;
    // The queue the HAL's change blocks run on, and the blocks.
    dispatch_queue_t queue;
    AudioObjectPropertyListenerBlock listener;
    bool listening;
    // Raised by a change block; the drain rescans.
    atomic_bool changed;
    // Room for a scan: device objects, endpoints, specs, one read.
    AudioObjectID* objects;
    uint32_t objectCapacity;
    maudCoreAudioEndpoint* endpoints;
    maudDeviceSpec* specs;
    unsigned char* scratch;
    maudCoreAudioStream* streams;
    // The data sources listened to, one per scanned endpoint at most.
    maudCoreAudioWatch* watched;
    uint32_t watchedCount;
    uint32_t watchCapacity;
    size_t bytes;
} maudCoreAudio;

// A HAL property's address on the main element.
static inline AudioObjectPropertyAddress maudCoreAudioAddress(AudioObjectPropertySelector selector,
                                                              AudioObjectPropertyScope scope)
{
    return (AudioObjectPropertyAddress){selector, scope, MAUD_COREAUDIO_ELEMENT_MAIN};
}

#endif // MAUL_AUDIO_SRC_COREAUDIO_CORE_H
