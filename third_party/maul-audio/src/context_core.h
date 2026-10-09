// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// What a context and its streams hold. The context composes its parts:
// the def it was made with, its backend, its device table, its stream
// table and its notification queue.

#ifndef MAUL_AUDIO_SRC_CONTEXT_CORE_H
#define MAUL_AUDIO_SRC_CONTEXT_CORE_H

#include "period.h"

#include "maul-audio/context.h"
#include "maul-audio/device.h"
#include "maul-audio/notification.h"
#include "maul-audio/stream.h"

#include <stdatomic.h>
#include <stdbool.h>

typedef struct maudBackend maudBackend;

// The run state the rendering thread reads.
enum
{
    maud_streamIdle = 0,
    maud_streamRunning = 1,
};

// Where a stream stands, as the control thread keeps it.
typedef struct maudStreamBinding
{
    // The device it was opened on; the null id when it follows a default.
    maudDeviceId requested;
    // The device it is on; the null id while it has none.
    maudDeviceId current;
    bool started;
    maudSuspendReason suspension;
    // The platform has not granted access yet; the stream cannot run.
    bool awaitingPermission;
} maudStreamBinding;

// A stream: what it was asked for, what it runs at, where it stands, and
// the state the rendering thread and the control thread share.
typedef struct maudStreamCore
{
    maudStreamDef def;
    maudStreamFormat format;
    maudStreamBinding binding;
    maudPeriod period;
    // The duplex pair the stream is a half of, numbered within its
    // context from 1, or 0; set before the backend attaches it.
    uint32_t duplexGroup;
    // The platform's report of voice processing (maudReportVoice): its
    // active parts, and whether it reported.
    _Atomic(uint8_t) voiceActive;
    atomic_bool voiceReported;
    // Underruns and overruns the platform revealed (xrun.h).
    _Atomic(uint64_t) underruns;
    _Atomic(uint64_t) overruns;
    // What the platform does with the stream's spatialized mark
    // (maudSpatialMark), as the backend says.
    _Atomic(uint8_t) spatialMark;
    // Whether the stream keeps others off its device: asked for, or a
    // backend's report (as for an ALSA hardware PCM).
    bool exclusive;
    // Bytes of period samples, as allocated.
    size_t sampleBytes;
    // Running when started and not suspended.
    _Atomic(uint8_t) state;
    // The rate blocks carry, published by the control thread.
    _Atomic(uint32_t) blockRate;
    // The thread rendering the stream, or 0.
    _Atomic(uintptr_t) renderingThread;
    // Frames moved to or from the device.
    _Atomic(uint64_t) position;
    // The clock stamp (clock.h): a frame, its host time and the latency,
    // under a sequence counter that is odd while it is written.
    _Atomic(uint32_t) clockSequence;
    _Atomic(uint64_t) clockPosition;
    _Atomic(int64_t) clockHost;
    _Atomic(int64_t) clockLatency;
} maudStreamCore;

typedef struct maudStreamSlot maudStreamSlot;

// The joint of a duplex stream (duplex.c): its output half, which the
// host names, and its hidden input half, and the ring between them.
// One allocation of bytes holds it, the ring and the input block.
typedef struct maudDuplex
{
    maudStreamSlot* output;
    maudStreamSlot* input;
    // The host's callback and user, called by the output half.
    maudStreamCallback callback;
    void* user;
    // Frames of channels samples; capacity is a power of two.
    float* ring;
    uint32_t capacity;
    uint32_t channels;
    uint32_t periodFrames;
    // Frames written by the input half and read by the output half.
    _Atomic(uint32_t) written;
    _Atomic(uint32_t) read;
    // The input has delivered; shortfalls count from then on.
    atomic_bool primed;
    _Atomic(uint64_t) slipped;
    // The input frames handed to the host with each output period.
    float* block;
    size_t bytes;
} maudDuplex;

struct maudStreamSlot
{
    maudStreamCore core;
    uint32_t generation;
    bool live;
    // The duplex joint this slot is a half of, or NULL.
    maudDuplex* duplex;
    // The input half of a duplex stream, which the host never names.
    bool hidden;
};

typedef struct maudStreamTable
{
    maudStreamSlot* slots;
    uint32_t capacity;
    // The duplex pairs numbered so far.
    uint32_t duplexGroups;
} maudStreamTable;

// A device's name or key: bytes in the context's text storage.
typedef struct maudDeviceText
{
    char* bytes;
    uint32_t length;
} maudDeviceText;

typedef struct maudDeviceSlot
{
    // The default flags are not kept here; the table's defaults are.
    maudDeviceInfo info;
    maudDeviceText name;
    maudDeviceText key;
    uint32_t generation;
    bool live;
} maudDeviceSlot;

typedef struct maudDeviceTable
{
    maudDeviceSlot* slots;
    uint32_t capacity;
    // The default device per direction and role.
    maudDeviceId defaults[2][2];
} maudDeviceTable;

typedef struct maudNotificationQueue
{
    maudNotification* records;
    uint32_t capacity;
    uint32_t head;
    uint32_t count;
} maudNotificationQueue;

struct maudContext
{
    maudContextDef def;
    const maudBackend* backend;
    maudDeviceTable devices;
    maudStreamTable streams;
    maudNotificationQueue notifications;
    // The backend's own state, or NULL.
    void* native;
    // Bytes of the context's block, as allocated.
    size_t bytes;
    // The platform holds the context's audio until the user acts.
    bool held;
    // The host suspended the context (maudSetContextSuspended).
    bool hostSuspended;
    // The audio focus last reported (focus.h).
    maudFocus focus;
    _Atomic(uint64_t) misuse;
};

#endif // MAUL_AUDIO_SRC_CONTEXT_CORE_H
