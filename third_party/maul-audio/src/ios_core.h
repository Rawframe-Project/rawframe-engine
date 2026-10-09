// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The iOS backend's state, shared by its context, stream and session
// modules.

#ifndef MAUL_AUDIO_SRC_IOS_CORE_H
#define MAUL_AUDIO_SRC_IOS_CORE_H

#include "apple_objects.h"
#include "context_core.h"
#include "device.h"

#include <AudioToolbox/AudioToolbox.h>
#include <stdatomic.h>

// Bytes kept for an input port's key and name, in UTF-8.
#define MAUD_IOS_PORT_BYTES 128u

// One of the session's available inputs: its key ("port:" and its UID),
// its name and the form it leads to.
typedef struct maudIosPort
{
    char key[MAUD_IOS_PORT_BYTES];
    char name[MAUD_IOS_PORT_BYTES];
    maudDeviceForm form;
} maudIosPort;

// A stream's RemoteIO unit, run by the system's IO thread while it
// plays.
typedef struct maudIosStream
{
    maudStreamCore* core;
    AudioComponentInstance unit;
    bool playing;
    // What the session adds between a buffer's host time and the
    // speaker, or between the microphone and it, in nanoseconds.
    int64_t sessionLatency;
    // An input stream's buffer list, with room for the most frames one
    // render brings, as allocated.
    AudioBufferList* captured;
    size_t capturedBytes;
    // The context, for the host time base.
    const struct maudIos* owner;
    // The channels the unit captures when fewer than the stream's: the
    // voice-processing unit captures one, spread to every channel. 0 for
    // the stream's own.
    uint32_t unitChannels;
    // A voiced duplex stream's halves share the input half's
    // voice-processing unit and point at each other; the output half has
    // no unit of its own.
    struct maudIosStream* voicePartner;
    // The sample time the next IO cycle should start at, or a negative
    // value before the first: a later start skipped cycles.
    Float64 nextSampleTime;
    // An object stream's spatial mixer, which renders for the unit; its
    // mixer is NULL for other streams.
    maudAppleObjects objects;
} maudIosStream;

// What the context's streams need of the session: outputs and inputs
// with units, a voiced duplex stream's (Voice-Processing I/O), and
// whether one plays or a unit is being made.
typedef struct maudIosUse
{
    bool outputs;
    bool inputs;
    bool voiced;
    bool running;
} maudIosUse;

// The session as the context last set it (ios_session.m).
typedef struct maudIosSession
{
    // The session's category and options were set, and whether it is
    // active.
    bool configured;
    bool active;
    // The focus the host asked for, which decides whether the
    // session mixes with others.
    maudFocusRequest focus;
    // What the category was set for.
    maudIosUse use;
} maudIosSession;

// What the session reports on the main thread, for the drain: whether
// an interruption began since the last drain, the last interruption
// event (0 none since the last drain, 1 began, 2 ended with the hint to
// resume, 3 ended without it), and a route change. An interruption that
// begins and ends between two drains still holds and releases the
// streams, as iOS stopped their units meanwhile.
typedef struct maudIosSignals
{
    atomic_bool began;
    atomic_int interruption;
    atomic_bool routeChanged;
} maudIosSignals;

// The signals' values.
#define MAUD_IOS_INTERRUPTION_BEGAN   1
#define MAUD_IOS_INTERRUPTION_RESUME  2
#define MAUD_IOS_INTERRUPTION_STOPPED 3

typedef struct maudIos
{
    maudContext* context;
    maudIosStream* streams;
    maudIosSession session;
    maudIosSignals signals;
    // The session observer (ios_session.m), retained.
    void* observer;
    // Room for a scan: the available inputs and the specs.
    maudIosPort* ports;
    maudDeviceSpec* specs;
    // The session's rate and output channels when the context opened.
    uint32_t rate;
    uint32_t channels;
    // mach_absolute_time's units, for the units' host times.
    uint32_t timebaseNumer;
    uint32_t timebaseDenom;
    size_t bytes;
} maudIos;

#endif // MAUL_AUDIO_SRC_IOS_CORE_H
