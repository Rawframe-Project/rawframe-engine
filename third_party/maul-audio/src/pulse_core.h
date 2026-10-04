// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The PulseAudio backend's state, shared by its connection and its
// streams.

#ifndef MAUL_AUDIO_SRC_PULSE_CORE_H
#define MAUL_AUDIO_SRC_PULSE_CORE_H

#include "context_core.h"
#include "pulse_api.h"
#include "worker.h"

#include <stdatomic.h>

// How long creation waits for the server, and a stream for its
// connection.
#define MAUD_PULSE_DEADLINE_NS 2000000000ll
// How long after a lost or refused connection the next one is tried.
#define MAUD_PULSE_RETRY_NS 500000000ll
// How many loop iterations one pump takes at most.
#define MAUD_PULSE_PUMP_ITERATIONS 64
// The rate of a native stream with no device yet.
#define MAUD_PULSE_FALLBACK_RATE 48000u
// Bytes of a default device's name.
#define MAUD_PULSE_NAME_BYTES 256

typedef struct maudPulse maudPulse;

// One sink or source, by PulseAudio's index, which is per direction.
typedef struct maudPulseNode
{
    maudDeviceId device;
    uint32_t index;
    maudDirection direction;
    bool used;
} maudPulseNode;

// The connection to the server and what its first queries owe.
typedef struct maudPulseServer
{
    pa_context* context;
    // Queries of the first listing still unanswered.
    int pending;
    bool ready;
    // The server went away; the context is dropped after the iteration
    // that reported it, and a new one is tried from nextAttempt on.
    bool lost;
    int64_t nextAttempt;
    char defaultNames[2][MAUD_PULSE_NAME_BYTES];
} maudPulseServer;

// A stream's own connection: its loop, context and pa_stream, run by
// its thread while the stream runs and by the control thread otherwise,
// never both at once.
typedef struct maudPulseStream
{
    const maudPulseApi* api;
    maudStreamCore* core;
    pa_mainloop* loop;
    pa_context* context;
    pa_stream* stream;
    maudWorker worker;
    // Asks the thread to return; set by the control thread.
    atomic_bool quit;
    bool threadRunning;
    bool used;
} maudPulseStream;

struct maudPulse
{
    maudPulseApi api;
    maudContext* context;
    pa_mainloop* loop;
    maudPulseServer server;
    maudPulseNode* nodes;
    uint32_t nodeCapacity;
    maudPulseStream* streams;
    size_t bytes;
    // Whether libpulse's port structures have their type: 14.0 or later.
    bool portTypes;
};

// The monotonic clock in nanoseconds.
int64_t maudPulseNow(void);

#endif // MAUL_AUDIO_SRC_PULSE_CORE_H
