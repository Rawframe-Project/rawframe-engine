// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Contexts: creation from a def, the stream table, and the checks that
// keep control calls and library allocations off the audio thread.

#include "context.h"

#include "allocator.h"
#include "backend.h"
#include "invariant.h"
#include "thread.h"

#define CONTEXT_DEF_COOKIE 0x6D616378u
#define MIN_RATE           8000u
#define MAX_RATE           384000u

maudContextDef maudDefaultContextDef(void)
{
    return (maudContextDef){
        .cookie = CONTEXT_DEF_COOKIE,
        .allocator = {0},
        .limits = {.streams = 8,
                   .periodFrames = 8192,
                   .devices = 32,
                   .notifications = 256,
                   .deviceTextBytes = 256},
        .backend = maud_backendNative,
        .offlineSampleRate = 48000,
    };
}

static bool DefValid(const maudContextDef* def)
{
    return def->cookie == CONTEXT_DEF_COOKIE && maudIsAllocatorValid(&def->allocator) &&
           def->limits.streams != 0 && def->limits.periodFrames != 0 && def->limits.devices != 0 &&
           def->limits.notifications >= 2 && def->limits.deviceTextBytes != 0 &&
           def->offlineSampleRate >= MIN_RATE && def->offlineSampleRate <= MAX_RATE &&
           (def->androidJavaVm == nullptr) == (def->androidContext == nullptr) &&
           def->backend <= maud_backendPrivate;
}

// The backend of a kind in this build, or NULL.
static const maudBackend* BackendOfKind(maudBackendKind kind)
{
    switch (kind)
    {
    case maud_backendOffline:
        return maudGetOfflineBackend();
#if defined(MAUD_HAVE_PIPEWIRE)
    case maud_backendPipewire:
        return maudGetPipewireBackend();
#endif
#if defined(MAUD_HAVE_PULSE)
    case maud_backendPulse:
        return maudGetPulseBackend();
#endif
#if defined(MAUD_HAVE_ALSA)
    case maud_backendAlsa:
        return maudGetAlsaBackend();
#endif
#if defined(MAUD_HAVE_WASAPI)
    case maud_backendWasapi:
        return maudGetWasapiBackend();
#endif
#if defined(MAUD_HAVE_COREAUDIO)
    case maud_backendCoreAudio:
        return maudGetCoreAudioBackend();
#elif defined(MAUD_HAVE_IOS)
    case maud_backendCoreAudio:
        return maudGetIosBackend();
#endif
#if defined(MAUD_HAVE_AAUDIO)
    case maud_backendAaudio:
        return maudGetAaudioBackend();
#endif
#if defined(__EMSCRIPTEN__)
    case maud_backendWeb:
        return maudGetWebBackend();
#endif
#if defined(MAUD_HAVE_PRIVATE_BACKEND)
    case maud_backendPrivate:
        return maudGetPrivateBackend();
#endif
    default:
        return nullptr;
    }
}

// What maud_backendNative tries, in order: a private backend first
// where one is built.
#if defined(MAUD_HAVE_PRIVATE_BACKEND)
#define PRIVATE_FIRST maud_backendPrivate,
#else
#define PRIVATE_FIRST
#endif
#if defined(_WIN32)
static const maudBackendKind s_nativeOrder[] = {PRIVATE_FIRST maud_backendWasapi};
#elif defined(__ANDROID__)
static const maudBackendKind s_nativeOrder[] = {PRIVATE_FIRST maud_backendAaudio};
#elif defined(__APPLE__)
static const maudBackendKind s_nativeOrder[] = {PRIVATE_FIRST maud_backendCoreAudio};
#elif defined(__EMSCRIPTEN__)
static const maudBackendKind s_nativeOrder[] = {PRIVATE_FIRST maud_backendWeb};
#else
static const maudBackendKind s_nativeOrder[] = {PRIVATE_FIRST maud_backendPipewire,
                                                maud_backendPulse, maud_backendAlsa};
#endif

static void InitStreams(maudStreamTable* streams)
{
    for (uint32_t i = 0; i < streams->capacity; ++i)
    {
        maudStreamSlot* slot = &streams->slots[i];
        slot->generation = 1;
        slot->live = false;
        slot->duplex = nullptr;
        slot->hidden = false;
        atomic_init(&slot->core.state, maud_streamIdle);
        atomic_init(&slot->core.blockRate, 0);
        atomic_init(&slot->core.spatialMark, maud_markNone);
        atomic_init(&slot->core.renderingThread, 0);
        atomic_init(&slot->core.position, 0);
        atomic_init(&slot->core.clockSequence, 0);
        atomic_init(&slot->core.clockPosition, 0);
        atomic_init(&slot->core.clockHost, 0);
        atomic_init(&slot->core.clockLatency, 0);
    }
}

// Gives each device slot its two places in the text storage.
static void InitDevices(maudDeviceTable* devices, char* text, uint32_t textBytes)
{
    for (uint32_t i = 0; i < devices->capacity; ++i)
    {
        devices->slots[i] = (maudDeviceSlot){
            .name = {.bytes = text + (size_t)i * 2u * textBytes},
            .key = {.bytes = text + ((size_t)i * 2u + 1u) * textBytes},
            .generation = 1,
        };
    }
}

// Opens one backend on a context with an empty device table. On
// success the queue is emptied: a new context reports changes from
// then on, not its starting devices.
static maudResult TryBackend(maudContext* context, const maudBackend* backend, char* text)
{
    InitDevices(&context->devices, text, context->def.limits.deviceTextBytes);
    context->devices.defaults[0][0] = context->devices.defaults[0][1] = (maudDeviceId){0, 0};
    context->devices.defaults[1][0] = context->devices.defaults[1][1] = (maudDeviceId){0, 0};
    context->backend = backend;
    maudResult result = backend->openContext(context);
    context->notifications = (maudNotificationQueue){
        .records = context->notifications.records,
        .capacity = context->notifications.capacity,
    };
    return result;
}

// Opens the backend the def names, or for maud_backendNative the first
// in the native order that answers.
static maudResult OpenBackend(maudContext* context, char* text)
{
    if (context->def.backend != maud_backendNative)
    {
        const maudBackend* backend = BackendOfKind(context->def.backend);
        return backend != nullptr ? TryBackend(context, backend, text) : maud_errorUnsupported;
    }
    maudResult result = maud_errorUnsupported;
    for (size_t i = 0; i < sizeof(s_nativeOrder) / sizeof(s_nativeOrder[0]); ++i)
    {
        const maudBackend* backend = BackendOfKind(s_nativeOrder[i]);
        result = backend != nullptr ? TryBackend(context, backend, text) : maud_errorUnsupported;
        if (result != maud_errorUnsupported)
        {
            return result;
        }
    }
    return result;
}

maudResult maudCreateContext(const maudContextDef* def, maudContext** contextOut)
{
    if (contextOut == nullptr)
    {
        return maud_errorInvalid;
    }
    *contextOut = nullptr;
    if (def == nullptr || !DefValid(def))
    {
        return maud_errorInvalid;
    }
    maudLayout layout = {0};
    size_t contextOffset = maudLayoutAdd(&layout, 1, sizeof(maudContext), alignof(maudContext));
    size_t streamsOffset = maudLayoutAdd(&layout, def->limits.streams, sizeof(maudStreamSlot),
                                         alignof(maudStreamSlot));
    size_t devicesOffset = maudLayoutAdd(&layout, def->limits.devices, sizeof(maudDeviceSlot),
                                         alignof(maudDeviceSlot));
    size_t recordsOffset = maudLayoutAdd(&layout, def->limits.notifications,
                                         sizeof(maudNotification), alignof(maudNotification));
    size_t textOffset =
        maudLayoutAdd(&layout, (size_t)def->limits.devices * 2u, def->limits.deviceTextBytes, 1);
    MAUD_ASSERT(!layout.overflow);
    unsigned char* block = maudAllocate(&def->allocator, layout.size, alignof(maudContext));
    if (block == nullptr)
    {
        return maud_errorCapacity;
    }
    maudContext* context = (maudContext*)(block + contextOffset);
    *context = (maudContext){
        .def = *def,
        .devices = {.slots = (maudDeviceSlot*)(block + devicesOffset),
                    .capacity = def->limits.devices},
        .streams = {.slots = (maudStreamSlot*)(block + streamsOffset),
                    .capacity = def->limits.streams},
        .notifications = {.records = (maudNotification*)(block + recordsOffset),
                          .capacity = def->limits.notifications},
        .bytes = layout.size,
    };
    atomic_init(&context->misuse, 0);
    InitStreams(&context->streams);
    maudResult result = OpenBackend(context, (char*)(block + textOffset));
    if (result != maud_success)
    {
        maudRelease(&context->def.allocator, block, layout.size, alignof(maudContext));
        return result;
    }
    *contextOut = context;
    return maud_success;
}

maudResult maudDestroyContext(maudContext* context)
{
    if (context == nullptr)
    {
        return maud_success;
    }
    if (maudIsRenderingThread(context))
    {
        maudCountMisuse(context);
        return maud_errorState;
    }
    for (uint32_t i = 0; i < context->streams.capacity; ++i)
    {
        if (context->streams.slots[i].live)
        {
            maudReleaseStream(context, &context->streams.slots[i]);
        }
    }
    if (context->backend->closeContext != nullptr)
    {
        context->backend->closeContext(context);
    }
    maudAllocator allocator = context->def.allocator;
    maudRelease(&allocator, context, context->bytes, alignof(maudContext));
    return maud_success;
}

maudBackendKind maudGetContextBackend(const maudContext* context)
{
    return context->backend->kind;
}

uint64_t maudGetContextMisuse(const maudContext* context)
{
    return atomic_load_explicit(&context->misuse, memory_order_relaxed);
}

void* maudContextAllocate(maudContext* context, size_t size, size_t alignment)
{
    MAUD_ASSERT(!maudIsRenderingThread(context));
    return maudAllocate(&context->def.allocator, size, alignment);
}

void maudContextRelease(maudContext* context, void* memory, size_t size, size_t alignment)
{
    MAUD_ASSERT(!maudIsRenderingThread(context));
    maudRelease(&context->def.allocator, memory, size, alignment);
}

bool maudIsRenderingThread(const maudContext* context)
{
    uintptr_t self = maudCurrentThread();
    for (uint32_t i = 0; i < context->streams.capacity; ++i)
    {
        // Only the atomic is read: the audio thread may ask while the
        // control thread creates or destroys another stream.
        const maudStreamSlot* slot = &context->streams.slots[i];
        if (atomic_load_explicit(&slot->core.renderingThread, memory_order_acquire) == self)
        {
            return true;
        }
    }
    return false;
}

void maudCountMisuse(maudContext* context)
{
    atomic_fetch_add_explicit(&context->misuse, 1, memory_order_relaxed);
}

maudStreamSlot* maudFindStream(const maudContext* context, maudStreamId stream)
{
    if (stream.index1 == 0 || stream.index1 > context->streams.capacity)
    {
        return nullptr;
    }
    maudStreamSlot* slot = &context->streams.slots[stream.index1 - 1];
    // A duplex stream's input half has no name of its own.
    return slot->live && !slot->hidden && slot->generation == stream.generation ? slot : nullptr;
}

maudDeviceSlot* maudFindDevice(const maudContext* context, maudDeviceId device)
{
    if (device.index1 == 0 || device.index1 > context->devices.capacity)
    {
        return nullptr;
    }
    maudDeviceSlot* slot = &context->devices.slots[device.index1 - 1];
    return slot->live && slot->generation == device.generation ? slot : nullptr;
}

maudStreamSlot* maudFindFreeStreamSlot(const maudContext* context)
{
    for (uint32_t i = 0; i < context->streams.capacity; ++i)
    {
        if (!context->streams.slots[i].live)
        {
            return &context->streams.slots[i];
        }
    }
    return nullptr;
}

maudStreamId maudStreamIdOf(const maudContext* context, const maudStreamSlot* slot)
{
    // What happens to a duplex stream's input half happens to the stream.
    if (slot->hidden)
    {
        slot = slot->duplex->output;
    }
    uint32_t index = (uint32_t)(slot - context->streams.slots);
    return (maudStreamId){index + 1, slot->generation};
}

static void ReleaseHalf(maudContext* context, maudStreamSlot* slot)
{
    if (context->backend->detachStream != nullptr)
    {
        context->backend->detachStream(context, slot);
    }
    maudContextRelease(context, slot->core.period.samples, slot->core.sampleBytes,
                       MAUD_STREAM_STORAGE_ALIGN);
    slot->live = false;
    slot->duplex = nullptr;
    slot->hidden = false;
    // A generation of 0 never names a stream, so it is skipped on wrap.
    slot->generation = slot->generation == UINT32_MAX ? 1 : slot->generation + 1;
}

void maudReleaseStream(maudContext* context, maudStreamSlot* slot)
{
    MAUD_ASSERT(slot->live);
    maudDuplex* duplex = slot->duplex;
    if (duplex == nullptr)
    {
        ReleaseHalf(context, slot);
        return;
    }
    // A duplex stream goes whole: its halves, then the joint.
    ReleaseHalf(context, duplex->output);
    if (duplex->input->live)
    {
        ReleaseHalf(context, duplex->input);
    }
    maudContextRelease(context, duplex, duplex->bytes, alignof(maudDuplex));
}

maudResult maudResumeContext(maudContext* context)
{
    if (context == nullptr)
    {
        return maud_errorInvalid;
    }
    if (maudIsRenderingThread(context))
    {
        maudCountMisuse(context);
        return maud_errorState;
    }
    if (context->backend->resumeContext != nullptr)
    {
        context->backend->resumeContext(context);
    }
    return maud_success;
}
