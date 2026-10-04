// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// What the PipeWire backend holds for a context: the loaded library,
// the connection, the default metadata, one entry per device node and
// one per stream, in the context's stream slot order.

#ifndef MAUL_AUDIO_SRC_PIPEWIRE_CORE_H
#define MAUL_AUDIO_SRC_PIPEWIRE_CORE_H

#include "context_core.h"
#include "pipewire_api.h"

// How long creation and stream opening wait for PipeWire's answers.
#define MAUD_PIPEWIRE_DEADLINE_NS 2000000000ll
// How long after a failed connection the next one is tried.
#define MAUD_PIPEWIRE_RETRY_NS 500000000ll
// How many loop iterations one pump takes at most.
#define MAUD_PIPEWIRE_PUMP_ITERATIONS 64
// The graph rate until the settings metadata gives it.
#define MAUD_PIPEWIRE_FALLBACK_RATE 48000u
// Bytes of a default device's node name.
#define MAUD_PIPEWIRE_NAME_BYTES 256

typedef struct maudPipewire maudPipewire;

// One sink or source node and the proxy that reports its formats.
typedef struct maudPipewireNode
{
    maudPipewire* owner;
    struct pw_proxy* proxy;
    struct spa_hook listener;
    // The null id until the node's info arrives and adds the device.
    maudDeviceId device;
    uint32_t globalId;
    maudDirection direction;
    // Its own form factor, and the card and card profile device it is
    // on, whose route may say more.
    maudDeviceForm factorForm;
    bool hasCard;
    uint32_t cardId;
    int32_t profileDevice;
    bool used;
} maudPipewireNode;

// How many routes a card keeps: one per profile device and direction.
#define MAUD_PIPEWIRE_CARD_ROUTES 8

// One active route: the card profile device it serves, its direction
// and the form its port type names.
typedef struct maudPipewireRoute
{
    int32_t device;
    maudDirection direction;
    maudDeviceForm form;
} maudPipewireRoute;

// One card's Device proxy and its active routes, filled by
// pipewire_card.
typedef struct maudPipewireCard
{
    maudPipewire* owner;
    struct pw_proxy* proxy;
    struct spa_hook listener;
    uint32_t globalId;
    maudPipewireRoute routes[MAUD_PIPEWIRE_CARD_ROUTES];
    uint32_t routeCount;
    bool used;
} maudPipewireCard;

// The connection: the loop, the core and the registry.
typedef struct maudPipewireConnection
{
    struct pw_loop* loop;
    struct pw_context* context;
    struct pw_core* core;
    struct spa_hook coreListener;
    struct pw_registry* registry;
    struct spa_hook registryListener;
    int pendingSync;
    bool synced;
    // The daemon went away; the core is dropped after the iteration
    // that reported it, and a new one is tried from nextAttempt on.
    bool lost;
    int64_t nextAttempt;
} maudPipewireConnection;

// The default metadata and the node names it gives per direction.
typedef struct maudPipewireDefaults
{
    struct pw_proxy* metadata;
    struct spa_hook listener;
    char names[2][MAUD_PIPEWIRE_NAME_BYTES];
} maudPipewireDefaults;

// The settings metadata and the graph rate it gives: the forced rate
// when one is set, otherwise the clock rate.
typedef struct maudPipewireClock
{
    struct pw_proxy* metadata;
    struct spa_hook listener;
    uint32_t clockRate;
    uint32_t forceRate;
    uint32_t graphRate;
} maudPipewireClock;

// One stream: the PipeWire stream and the core it feeds. The process
// callback reads core from libpipewire's data thread.
typedef struct maudPipewireStream
{
    maudPipewire* owner;
    maudStreamCore* core;
    struct pw_stream* stream;
    struct spa_hook listener;
    enum pw_stream_state state;
    bool used;
    // The graph's ticks at the last cycle and the frames it moved, to see
    // a skipped cycle; forgotten when the control thread sets
    // forgetTicks, as when the stream is activated.
    uint64_t lastTicks;
    uint32_t lastFrames;
    atomic_bool forgetTicks;
    // Process callbacks in flight on the data thread, and the control
    // thread's word that the stream is going: pw_stream_destroy does not
    // always wait for a callback already running, so detaching does.
    atomic_uint inside;
    atomic_bool closing;
} maudPipewireStream;

struct maudPipewire
{
    maudPipewireApi api;
    maudContext* context;
    maudPipewireConnection connection;
    maudPipewireDefaults defaults;
    maudPipewireClock clock;
    maudPipewireNode* nodes;
    uint32_t nodeCapacity;
    maudPipewireCard* cards;
    uint32_t cardCapacity;
    maudPipewireStream* streams;
    size_t bytes;
};

// The monotonic clock, in nanoseconds.
int64_t maudPipewireNow(void);

// Waits on the calling thread until PipeWire has answered everything
// asked so far, or the deadline passes. False on the deadline or a
// lost connection.
bool maudPipewireRoundtrip(maudPipewire* pipewire, int64_t deadline);

#endif // MAUL_AUDIO_SRC_PIPEWIRE_CORE_H
