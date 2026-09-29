// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The context: its defs, its one block of memory, and mwinRun. The block
// holds the context, its critical and global rings, the locales, the
// monitor slots, the window slots, then per window a record ring per
// class, its text storage, its request slots and its two title buffers.

#include "maul-window/context.h"

#include "allocator.h"
#include "core.h"

#include <stdalign.h>
#include <stdlib.h>
#include <string.h>

#define CONTEXT_DEF_COOKIE 0x6D776378u
#define APP_DEF_COOKIE     0x6D776170u

// The notification classes of a window that coalesce, plus created and
// destroyed: with its completions, the most notifications that can wait
// for it.
#define FIXED_RECORDS 15

// The classes of the context's own notifications that coalesce, beyond
// three records per monitor (its addition, a change, its removal).
#define GLOBAL_CLASSES 4

// The context's own notifications a gamepad can have waiting: its
// addition, a change, a reset and its removal.
#define GAMEPAD_RECORDS 4

mwinContextDef mwinDefaultContextDef(void)
{
    mwinContextDef def = {0};
    def.cookie = CONTEXT_DEF_COOKIE;
    def.limits.windows = 8;
    def.limits.requestsPerWindow = 32;
    def.limits.notificationsPerWindow = 256;
    def.limits.titleBytes = 1024;
    def.limits.inputPerWindow = 256;
    def.limits.textBytesPerWindow = 4096;
    def.limits.monitors = 16;
    def.limits.localeBytes = 256;
    def.limits.gamepads = 8;
    def.limits.clipboardBytes = 1u << 20;
    def.limits.dropBytes = 1u << 20;
    def.limits.droppedFiles = 256;
    def.limits.dialogFiles = 256;
    def.limits.dialogBytes = 1u << 20;
    def.backend = mwin_backendNative;
    return def;
}

mwinAppDef mwinDefaultAppDef(void)
{
    mwinAppDef def = {0};
    def.cookie = APP_DEF_COOKIE;
    def.context = mwinDefaultContextDef();
    return def;
}

static bool IsDefValid(const mwinAppDef* def)
{
    const mwinContextDef* context = &def->context;
    const mwinLimits* limits = &context->limits;
    return def->cookie == APP_DEF_COOKIE && def->init != nullptr && def->frame != nullptr &&
           context->cookie == CONTEXT_DEF_COOKIE && mwinIsAllocatorValid(&context->allocator) &&
           limits->windows > 0 && limits->requestsPerWindow > 0 && limits->titleBytes > 0 &&
           limits->inputPerWindow > 0 && limits->textBytesPerWindow > 0 && limits->monitors > 0 &&
           limits->notificationsPerWindow >=
               3 * limits->monitors + GAMEPAD_RECORDS * limits->gamepads + GLOBAL_CLASSES &&
           limits->notificationsPerWindow >= limits->requestsPerWindow + FIXED_RECORDS &&
           context->backend <= mwin_backendTest;
}

static size_t RoundUp(size_t size)
{
    return (size + alignof(max_align_t) - 1) & ~(alignof(max_align_t) - 1);
}

// The records a ring of a class holds.
static size_t RingRecords(const mwinLimits* limits, int kind)
{
    return kind == mwin_classNotification ? limits->notificationsPerWindow : limits->inputPerWindow;
}

static size_t RingBytes(size_t records)
{
    return RoundUp(records * sizeof(mwinEvent)) + RoundUp(records * sizeof(uint64_t));
}

// The records the critical ring holds: a surface record per window and a
// lifecycle record, each class coalescing.
static size_t CriticalRecords(const mwinLimits* limits)
{
    return (size_t)limits->windows + 1;
}

// Points a ring at its storage and returns the storage after it.
static unsigned char* LayRing(mwinRing* ring, unsigned char* storage, size_t records)
{
    ring->events = (mwinEvent*)storage;
    ring->sequences = (uint64_t*)(storage + RoundUp(records * sizeof(mwinEvent)));
    ring->capacity = (uint16_t)records;
    return storage + RingBytes(records);
}

// The bytes one window slot's storage takes.
static size_t WindowBytes(const mwinLimits* limits)
{
    size_t rings = 0;
    for (int kind = 0; kind < MWIN_CLASSES; kind++)
    {
        rings += RingBytes(RingRecords(limits, kind));
    }
    return rings + RoundUp(limits->requestsPerWindow * sizeof(mwinRequest)) +
           2 * RoundUp(limits->titleBytes) + RoundUp(limits->textBytesPerWindow);
}

// Points each window slot at its part of the block after the slots.
static void Lay(mwinContext* context, unsigned char* storage)
{
    const mwinLimits* limits = &context->limits;
    for (uint32_t i = 0; i < limits->windows; i++)
    {
        mwinWindow* window = &context->windows[i];
        for (int kind = 0; kind < MWIN_CLASSES; kind++)
        {
            storage = LayRing(&window->rings[kind], storage, RingRecords(limits, kind));
        }
        window->text.bytes = (char*)storage;
        window->text.capacity = limits->textBytesPerWindow;
        storage += RoundUp(limits->textBytesPerWindow);
        window->requests = (mwinRequest*)storage;
        storage += RoundUp(limits->requestsPerWindow * sizeof(mwinRequest));
        window->title = (char*)storage;
        storage += RoundUp(limits->titleBytes);
        window->pendingTitle = (char*)storage;
        storage += RoundUp(limits->titleBytes);
    }
}

#if defined(MAUL_WINDOW_WAYLAND) || defined(MAUL_WINDOW_X11)
// Whether an environment variable names something.
static bool IsSet(const char* name)
{
    const char* value = getenv(name);
    return value != nullptr && value[0] != '\0';
}
#endif

// The backends to try for a kind, in order: for the native one, Win32
// on Windows; on Linux Wayland where a Wayland session names its
// display, then X11 where DISPLAY names one (W7). Returns their count.
static int FindBackends(mwinBackendKind kind, const mwinBackendOps* backends[2])
{
    int count = 0;
#ifdef MAUL_WINDOW_TEST_BACKEND
    if (kind == mwin_backendTest)
    {
        backends[count++] = &mwinTestBackend;
    }
#endif
#ifdef MAUL_WINDOW_WAYLAND
    if (kind == mwin_backendNative && IsSet("WAYLAND_DISPLAY"))
    {
        backends[count++] = &mwinWaylandBackend;
    }
#endif
#ifdef MAUL_WINDOW_X11
    if (kind == mwin_backendNative && IsSet("DISPLAY"))
    {
        backends[count++] = &mwinX11Backend;
    }
#endif
#ifdef MAUL_WINDOW_WIN32
    if (kind == mwin_backendNative)
    {
        backends[count++] = &mwinWin32Backend;
    }
#endif
#ifdef MAUL_WINDOW_WEB
    if (kind == mwin_backendNative)
    {
        backends[count++] = &mwinWebBackend;
    }
#endif
    (void)kind;
    (void)backends;
    return count;
}

static mwinResult CreateContext(const mwinAppDef* def, mwinContext** contextOut)
{
    const mwinLimits* limits = &def->context.limits;
    size_t rings = RingBytes(CriticalRecords(limits)) + RingBytes(limits->notificationsPerWindow);
    size_t header = RoundUp(sizeof(mwinContext)) + rings + 2 * RingBytes(limits->inputPerWindow) +
                    RoundUp(limits->localeBytes) + RoundUp(limits->monitors * sizeof(mwinMonitor)) +
                    RoundUp(limits->gamepads * sizeof(mwinGamepad)) +
                    RoundUp(limits->windows * sizeof(mwinWindow));
    size_t size = header + limits->windows * WindowBytes(limits);
    unsigned char* block = mwinAllocate(&def->context.allocator, size, alignof(max_align_t));
    if (block == nullptr)
    {
        return mwin_errorCapacity;
    }
    memset(block, 0, size);
    mwinContext* context = (mwinContext*)block;
    context->allocator = def->context.allocator;
    context->memorySize = size;
    context->limits = *limits;
    // A copy: without Emscripten mwinRun returns while the program runs
    // on, and the def may have been the caller's local (mwin-0022).
    context->app = *def;
    unsigned char* storage = block + RoundUp(sizeof(mwinContext));
    storage = LayRing(&context->critical, storage, CriticalRecords(limits));
    storage = LayRing(&context->global, storage, limits->notificationsPerWindow);
    storage = LayRing(&context->gamepadRings[mwin_padRingButtons], storage, limits->inputPerWindow);
    storage = LayRing(&context->gamepadRings[mwin_padRingAxes], storage, limits->inputPerWindow);
    context->locales = (char*)storage;
    storage += RoundUp(limits->localeBytes);
    context->facts.textScale = 1.0f;
    context->monitors = (mwinMonitor*)storage;
    storage += RoundUp(limits->monitors * sizeof(mwinMonitor));
    context->gamepads = (mwinGamepad*)storage;
    storage += RoundUp(limits->gamepads * sizeof(mwinGamepad));
    context->windows = (mwinWindow*)storage;
    Lay(context, block + header);
    *contextOut = context;
    return mwin_success;
}

static void DestroyContext(mwinContext* context)
{
    mwinReleaseClipboard(context);
    mwinReleaseDrops(context);
    mwinReleaseDialogs(context);
    // Requests a platform never answered still hold their text.
    for (uint32_t slot = 0; slot < context->limits.windows; slot++)
    {
        for (uint32_t i = 0; i < context->limits.requestsPerWindow; i++)
        {
            mwinReleaseRequestData(context, &context->windows[slot].requests[i]);
        }
    }
    mwinAllocator allocator = context->allocator;
    mwinRelease(&allocator, context, context->memorySize, alignof(max_align_t));
}

mwinResult mwinRun(const mwinAppDef* def)
{
    if (def == nullptr || !IsDefValid(def))
    {
        return mwin_errorInvalid;
    }
    const mwinBackendOps* backends[2] = {nullptr, nullptr};
    int count = FindBackends(def->context.backend, backends);
    mwinResult status = mwin_errorUnsupported;
    // A backend that cannot reach its window system gives way to the
    // next, with a fresh context.
    for (int i = 0; i < count; i++)
    {
        mwinContext* context = nullptr;
        status = CreateContext(def, &context);
        if (status != mwin_success)
        {
            return status;
        }
        context->backend = backends[i];
        status = backends[i]->start(context);
        if (status == mwin_success)
        {
            status = backends[i]->run(context);
            if (context->loopOutlivesRun)
            {
                return status;
            }
            backends[i]->stop(context);
            DestroyContext(context);
            return status;
        }
        DestroyContext(context);
        if (status != mwin_errorUnsupported && status != mwin_errorPlatform)
        {
            return status;
        }
    }
    return status;
}

bool mwinStartProgram(mwinContext* context)
{
    const mwinAppDef* app = &context->app;
    context->inProgram = true;
    context->status = app->init(context, app->user);
    context->inProgram = false;
    context->running = context->status == mwin_success;
    return context->running;
}

bool mwinStepProgram(mwinContext* context, void (*pump)(mwinContext* context))
{
    if (!context->running || context->stopping)
    {
        return false;
    }
    mwinBeginPump(context);
    pump(context);
    if (context->stopping)
    {
        return false; // a critical frame asked to stop
    }
    const mwinAppDef* app = &context->app;
    context->inProgram = true;
    mwinFrameResult result = app->frame(context, app->user);
    context->inProgram = false;
    context->stopping = result != mwin_frameContinue;
    return !context->stopping;
}

mwinResult mwinEndProgram(mwinContext* context)
{
    const mwinAppDef* app = &context->app;
    context->running = false;
    if (app->quit != nullptr)
    {
        context->inProgram = true;
        app->quit(context, context->status, app->user);
        context->inProgram = false;
    }
    return context->status;
}

mwinResult mwinRunLoop(mwinContext* context, void (*pump)(mwinContext* context))
{
    if (mwinStartProgram(context))
    {
        while (mwinStepProgram(context, pump))
        {
        }
    }
    return mwinEndProgram(context);
}

void mwinFinishRun(mwinContext* context)
{
    context->backend->stop(context);
    DestroyContext(context);
}

void mwinRunCriticalFrame(mwinContext* context)
{
    if (context->inProgram || !context->running || context->stopping)
    {
        return;
    }
    const mwinAppDef* app = &context->app;
    context->inProgram = true;
    mwinFrameResult result = app->frame(context, app->user);
    context->inProgram = false;
    context->stopping = result != mwin_frameContinue;
}
