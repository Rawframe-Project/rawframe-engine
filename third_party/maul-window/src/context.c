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
#include "cursor.h"

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
    def.limits.cursors = 16;
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

// Every part of the context's block aligns as the block does.
#define PART alignof(max_align_t)

// The records a ring of a class holds.
static size_t RingRecords(const mwinLimits* limits, int kind)
{
    return kind == mwin_classNotification ? limits->notificationsPerWindow : limits->inputPerWindow;
}

// The records the critical ring holds: a surface record per window and a
// lifecycle record, each class coalescing.
static size_t CriticalRecords(const mwinLimits* limits)
{
    return (size_t)limits->windows + 1;
}

// Lays out a ring's records and their sequences, and points the ring at
// them once the block exists (block and ring NULL while sizing).
static void PlanRing(mwinLayout* layout, unsigned char* block, mwinRing* ring, size_t records)
{
    size_t events = mwinLayoutAdd(layout, records, sizeof(mwinEvent), PART);
    size_t sequences = mwinLayoutAdd(layout, records, sizeof(uint64_t), PART);
    if (block != nullptr)
    {
        ring->events = (mwinEvent*)(block + events);
        ring->sequences = (uint64_t*)(block + sequences);
        ring->capacity = (uint32_t)records;
    }
}

// Lays out a window slot's storage: its rings, text, requests and
// titles, pointing the slot at them once the block exists.
static void PlanWindow(mwinLayout* layout, const mwinLimits* limits, unsigned char* block,
                       mwinWindow* window)
{
    for (int kind = 0; kind < MWIN_CLASSES; kind++)
    {
        PlanRing(layout, block, block != nullptr ? &window->rings[kind] : nullptr,
                 RingRecords(limits, kind));
    }
    size_t text = mwinLayoutAdd(layout, limits->textBytesPerWindow, 1, PART);
    size_t requests = mwinLayoutAdd(layout, limits->requestsPerWindow, sizeof(mwinRequest), PART);
    size_t title = mwinLayoutAdd(layout, limits->titleBytes, 1, PART);
    size_t pendingTitle = mwinLayoutAdd(layout, limits->titleBytes, 1, PART);
    if (block != nullptr)
    {
        window->text.bytes = (char*)(block + text);
        window->text.capacity = limits->textBytesPerWindow;
        window->requests = (mwinRequest*)(block + requests);
        window->title = (char*)(block + title);
        window->pendingTitle = (char*)(block + pendingTitle);
    }
}

// Lays out the context's block: the context, its rings, locales,
// monitors, gamepads and window slots, then each slot's storage. Called
// without a block to size it, then with the block to point the context's
// parts into it, the same steps both times.
static void Plan(mwinLayout* layout, const mwinLimits* limits, unsigned char* block)
{
    mwinContext* context = (mwinContext*)block;
    (void)mwinLayoutAdd(layout, 1, sizeof(mwinContext), PART);
    PlanRing(layout, block, block != nullptr ? &context->critical : nullptr,
             CriticalRecords(limits));
    PlanRing(layout, block, block != nullptr ? &context->global : nullptr,
             limits->notificationsPerWindow);
    for (int ring = 0; ring < 2; ring++)
    {
        PlanRing(layout, block, block != nullptr ? &context->gamepadRings[ring] : nullptr,
                 limits->inputPerWindow);
    }
    size_t locales = mwinLayoutAdd(layout, limits->localeBytes, 1, PART);
    size_t monitors = mwinLayoutAdd(layout, limits->monitors, sizeof(mwinMonitor), PART);
    size_t gamepads = mwinLayoutAdd(layout, limits->gamepads, sizeof(mwinGamepad), PART);
    size_t windows = mwinLayoutAdd(layout, limits->windows, sizeof(mwinWindow), PART);
    size_t cursors = mwinLayoutAdd(layout, limits->cursors, sizeof(mwinCursor), PART);
    if (block != nullptr)
    {
        context->cursors = (mwinCursor*)(block + cursors);
        context->locales = (char*)(block + locales);
        context->monitors = (mwinMonitor*)(block + monitors);
        context->gamepads = (mwinGamepad*)(block + gamepads);
        context->windows = (mwinWindow*)(block + windows);
    }
    for (uint32_t i = 0; i < limits->windows && !layout->overflow; i++)
    {
        PlanWindow(layout, limits, block, block != nullptr ? &context->windows[i] : nullptr);
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
// on Windows, AppKit on macOS, UIKit on iOS; on Linux Wayland where a
// Wayland session names its display, then X11 where DISPLAY names one
// (mwin-0006). Returns their count.
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
#ifdef MAUL_WINDOW_MACOS
    if (kind == mwin_backendNative)
    {
        backends[count++] = &mwinMacBackend;
    }
#endif
#ifdef MAUL_WINDOW_IOS
    if (kind == mwin_backendNative)
    {
        backends[count++] = &mwinIOSBackend;
    }
#endif
#ifdef MAUL_WINDOW_WEB
    if (kind == mwin_backendNative)
    {
        backends[count++] = &mwinWebBackend;
    }
#endif
#ifdef MAUL_WINDOW_ANDROID
    if (kind == mwin_backendNative)
    {
        backends[count++] = &mwinAndroidBackend;
    }
#endif
    (void)kind;
    (void)backends;
    return count;
}

static mwinResult CreateContext(const mwinAppDef* def, mwinContext** contextOut)
{
    const mwinLimits* limits = &def->context.limits;
    mwinLayout layout = {0};
    Plan(&layout, limits, nullptr);
    unsigned char* block =
        layout.overflow ? nullptr : mwinAllocate(&def->context.allocator, layout.size, PART);
    if (block == nullptr)
    {
        return mwin_errorCapacity;
    }
    memset(block, 0, layout.size);
    mwinContext* context = (mwinContext*)block;
    context->allocator = def->context.allocator;
    context->memorySize = layout.size;
    context->misuse = &context->misuseCount;
    context->limits = *limits;
    // A copy: without Emscripten mwinRun returns while the program runs
    // on, and the def may have been the caller's local (mwin-0022).
    context->app = *def;
    context->facts.textScale = 1.0f;
    mwinLayout carving = {0};
    Plan(&carving, limits, block);
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

uint64_t mwinGetContextMisuse(const mwinContext* context)
{
    return context != nullptr ? *context->misuse : 0;
}

mwinResult mwinRun(const mwinAppDef* def)
{
    return mwinRunLaunched(def, nullptr);
}

mwinResult mwinRunLaunched(const mwinAppDef* def, void* launch)
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
        context->launch = launch;
        status = backends[i]->start(context);
        if (status == mwin_success)
        {
            status = backends[i]->run(context);
            if (context->loopOutlivesRun)
            {
                return status;
            }
            mwinReleaseCursors(context);
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
    if (context->inProgram)
    {
        // The program's own code spun the platform's loop (AppKit's, for
        // a modal panel or an input method): its frame is not over.
        return true;
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
    mwinReleaseCursors(context);
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
