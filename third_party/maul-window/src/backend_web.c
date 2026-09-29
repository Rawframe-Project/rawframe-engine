// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The web backend. The browser owns the loop: mwinRun calls init, then
// hands the frame to requestAnimationFrame through Emscripten's main
// loop and never returns; the frame that stops the program ends the
// loop, calls quit and frees the context. Each frame first takes the
// records the page queued since the last. The page is one monitor: the
// screen, at devicePixelRatio.

#include "allocator.h"
#include "backend.h"
#include "web.h"
#include "web_clipboard.h"
#include "web_drop.h"
#include "web_input.h"
#include "web_js.h"
#include "web_pad.h"
#include "web_page.h"
#include "web_services.h"
#include "web_text.h"
#include "web_window.h"

#include <math.h>
#include <string.h>

static mwinWebPlatform* PlatformOf(const mwinContext* context)
{
    return (mwinWebPlatform*)context->backendData;
}

static size_t PlatformBytes(const mwinContext* context)
{
    return sizeof(mwinWebPlatform) + context->limits.windows * sizeof(mwinWebWindow) +
           context->limits.textBytesPerWindow + 1u;
}

static uint64_t NowNs(void)
{
    return mwinWebNanoseconds(mwinWebNow());
}

// The screen as a monitor, in device pixels.
static void ReadScreen(mwinWebPlatform* platform)
{
    float screen[4];
    mwinWebScreen(screen);
    float scale = platform->scale;
    mwinMonitorInfo info = {0};
    info.bounds = (mwinPixelRect){0, 0, (uint32_t)lroundf(screen[0] * scale),
                                  (uint32_t)lroundf(screen[1] * scale)};
    info.workArea = (mwinPixelRect){0, 0, (uint32_t)lroundf(screen[2] * scale),
                                    (uint32_t)lroundf(screen[3] * scale)};
    info.scale = scale;
    info.primary = true;
    if (platform->monitor < 0)
    {
        platform->monitor = mwinAddMonitor(platform->context, &info, NowNs());
    }
    else
    {
        mwinChangeMonitor(platform->context, (uint32_t)platform->monitor, &info, NowNs());
    }
}

static void ReadFacts(mwinWebPlatform* platform)
{
    mwinContext* context = platform->context;
    mwinSystemFacts facts = {.textScale = 1.0f};
    facts.theme = (mwinTheme)mwinWebTheme();
    facts.reducedMotion = mwinWebReducedMotion();
    mwinSetSystemFacts(context, &facts, NowNs());
    // A list past the limit is left as it was.
    char locales[MWIN_WEB_LOCALE_BYTES];
    uint32_t length = mwinWebLocales(locales, sizeof(locales));
    if (length < sizeof(locales))
    {
        (void)mwinSetLocales(context, locales, length, NowNs());
    }
}

static void PostLifecycle(mwinContext* context, mwinEventType type)
{
    mwinEvent event = {.type = type, .timeNs = NowNs()};
    mwinPostGlobal(context, &event);
}

#ifdef __EMSCRIPTEN__
static void StopLoop(mwinContext* context)
{
    (void)context;
    emscripten_cancel_main_loop();
}
#else
// The page's loop without Emscripten (mwin-0022): each animation frame
// calls step with the context until it returns false, or until the loop
// is stopped.
typedef bool (*mwinWebStep)(mwinContext* context);

// clang-format off
EM_JS(void, mwinWebLoop, (mwinContext* context, mwinWebStep step), {
    const loops = Module.mwinWebLoops || (Module.mwinWebLoops = new Map());
    const loop = {running: true};
    loops.set(context, loop);
    const frame = () => {
        if (!loop.running) {
            return;
        }
        if (getWasmTableEntry(step)(context)) {
            requestAnimationFrame(frame);
        } else {
            loops.delete(context);
        }
    };
    requestAnimationFrame(frame);
});

EM_JS(void, mwinWebStopLoop, (const mwinContext* context), {
    const loop = Module.mwinWebLoops && Module.mwinWebLoops.get(context);
    if (loop) {
        loop.running = false;
        Module.mwinWebLoops.delete(context);
    }
});
// clang-format on

static void StopLoop(mwinContext* context)
{
    mwinWebStopLoop(context);
}
#endif

// A frame the page's loop may not run for a while, or may never run
// again, where the program can save what must survive. False when the
// frame stopped the program, which is over and its context freed.
static bool RunCriticalFrame(mwinContext* context)
{
    mwinRunCriticalFrame(context);
    if (!context->stopping || !context->running)
    {
        return true;
    }
    StopLoop(context);
    (void)mwinEndProgram(context);
    mwinFinishRun(context);
    return false;
}

// The page went away or came back: the program hears of it at once.
static void Lifecycle(mwinContext* context, int running)
{
    mwinWebPlatform* platform = PlatformOf(context);
    if ((running != 0) != platform->suspended)
    {
        return;
    }
    platform->suspended = running == 0;
    PostLifecycle(context, running != 0 ? mwin_eventResuming : mwin_eventSuspending);
    if (RunCriticalFrame(context))
    {
        PostLifecycle(context, running != 0 ? mwin_eventResumed : mwin_eventSuspended);
    }
}

static void Stop(mwinContext* context)
{
    mwinWebPlatform* platform = PlatformOf(context);
    mwinWebDetach(context);
    mwinRelease(&context->allocator, platform, PlatformBytes(context), alignof(max_align_t));
    context->backendData = nullptr;
}

static mwinResult Start(mwinContext* context)
{
    if (!mwinWebHasPage())
    {
        return mwin_errorPlatform;
    }
    unsigned char* block =
        mwinAllocate(&context->allocator, PlatformBytes(context), alignof(max_align_t));
    if (block == nullptr)
    {
        return mwin_errorCapacity;
    }
    memset(block, 0, PlatformBytes(context));
    mwinWebPlatform* platform = (mwinWebPlatform*)block;
    platform->windows = (mwinWebWindow*)(block + sizeof(mwinWebPlatform));
    platform->text = (char*)(platform->windows + context->limits.windows);
    platform->context = context;
    platform->monitor = -1;
    platform->scale = mwinWebScale();
    context->backendData = platform;
    mwinWebAttach(context, Lifecycle);
    mwinWebAttachInput(context);
#ifdef MAUL_WINDOW_GAMEPAD
    mwinWebPadsStart(&platform->pads, context);
#endif
    ReadScreen(platform);
    ReadFacts(platform);
    return mwin_success;
}

// Posts a type to every window the page shows.
static void PostToWindows(mwinWebPlatform* platform, mwinEventType type, uint64_t timeNs)
{
    mwinContext* context = platform->context;
    for (uint32_t i = 0; i < context->limits.windows; i++)
    {
        if (context->windows[i].status == mwin_slotLive && platform->windows[i].selectorLength > 0)
        {
            mwinEvent event = {.type = type, .timeNs = timeNs};
            mwinPost(context, i, &event);
        }
    }
}

// A new devicePixelRatio: the screen's pixels and every window's scale.
static void Rescale(mwinWebPlatform* platform, float scale, uint64_t timeNs)
{
    platform->scale = scale;
    ReadScreen(platform);
    mwinContext* context = platform->context;
    for (uint32_t i = 0; i < context->limits.windows; i++)
    {
        mwinWebWindow* window = &platform->windows[i];
        if (context->windows[i].status == mwin_slotLive && window->selectorLength > 0)
        {
            window->scale = scale;
            mwinEvent event = {.type = mwin_eventScaleChanged, .timeNs = timeNs};
            event.data.scale = (mwinScaleChange){scale, window->size};
            mwinPost(context, i, &event);
        }
    }
}

static void Pump(mwinContext* context)
{
    mwinWebPlatform* platform = PlatformOf(context);
    mwinWebCheckScale(context);
    mwinWebKeepAwake(platform, mwinWantsAwake(context));
    mwinWebRecord record;
    while (mwinWebNext(context, &record))
    {
        uint64_t timeNs = mwinWebNanoseconds(record.timeMs);
        switch (record.kind)
        {
        case mwin_webScaled:
            Rescale(platform, record.x, timeNs);
            break;
        case mwin_webVisibility:
            PostToWindows(platform, record.code != 0 ? mwin_eventRevealed : mwin_eventOccluded,
                          timeNs);
            break;
        case mwin_webFacts:
        case mwin_webLocales:
            ReadFacts(platform);
            break;
        case mwin_webClipboardWritten:
        case mwin_webClipboardRead:
            mwinWebHandleClipboardRecord(platform, &record);
            break;
        case mwin_webDrag:
        case mwin_webDropped:
            mwinWebHandleDropRecord(platform, &record);
            break;
        case mwin_webLayout:
        {
            mwinEvent event = {.type = mwin_eventKeyboardLayoutChanged, .timeNs = timeNs};
            mwinPostGlobal(context, &event);
            break;
        }
        default:
            if (record.slot < 0 || context->windows[record.slot].status != mwin_slotLive)
            {
                break;
            }
            if (record.kind == mwin_webCommit || record.kind == mwin_webPreedit)
            {
                mwinWebHandleTextRecord(platform, &record);
            }
            else if (record.kind >= mwin_webKey && record.kind <= mwin_webLockFailed)
            {
                mwinWebHandleInputRecord(platform, &record);
            }
            else
            {
                mwinWebHandleWindowRecord(platform, &record);
            }
            break;
        }
    }
#ifdef MAUL_WINDOW_GAMEPAD
    mwinWebPadsPump(&platform->pads, NowNs());
#endif
}

// A frame of the browser's: the program's frame, or its end. False once
// the program has ended and its context is freed.
static bool Step(mwinContext* context)
{
    if (mwinStepProgram(context, Pump))
    {
        return true;
    }
    StopLoop(context);
    (void)mwinEndProgram(context);
    mwinFinishRun(context);
    return false;
}

#ifdef __EMSCRIPTEN__
static void EmscriptenStep(void* data)
{
    (void)Step(data);
}
#endif

static mwinResult Run(mwinContext* context)
{
    if (!mwinStartProgram(context))
    {
        return mwinEndProgram(context);
    }
#ifdef __EMSCRIPTEN__
    // Unwinds the stack: mwinRun does not return.
    emscripten_set_main_loop_arg(EmscriptenStep, context, 0, true);
#else
    // Nothing can unwind the stack: mwinRun returns while the page's
    // frames run the program on, and the last of them frees the context.
    context->loopOutlivesRun = true;
    mwinWebLoop(context, Step);
#endif
    return mwin_success;
}

static uint64_t Now(const mwinContext* context)
{
    (void)context;
    return NowNs();
}

static mwinKey MapKeyCode(const mwinContext* context, mwinKeyCode code)
{
    return mwinWebMapKeyCode(context, code);
}

// A page is told no layout's name.
static mwinResult KeyboardLayout(const mwinContext* context, char* buffer, size_t capacity,
                                 size_t* lengthOut)
{
    (void)context;
    (void)buffer;
    (void)capacity;
    *lengthOut = 0;
    return mwin_success;
}

static void NativeHandles(const mwinContext* context, uint32_t slot, mwinNativeHandles* out)
{
    const mwinWebWindow* window = &PlatformOf(context)->windows[slot];
    out->platform = mwin_platformWeb;
    out->handles.web.selector = window->selector;
    out->handles.web.selectorLength = window->selectorLength;
    out->handles.web.accessibility = window->host;
    out->handles.web.accessibilityLength = window->hostLength;
}

static mwinResult Rumble(mwinContext* context, uint32_t slot, float low, float high,
                         uint32_t durationMs)
{
#ifdef MAUL_WINDOW_GAMEPAD
    return mwinWebPadsRumble(&PlatformOf(context)->pads, slot, low, high, durationMs);
#else
    (void)context;
    (void)slot;
    (void)low;
    (void)high;
    (void)durationMs;
    return mwin_errorUnsupported;
#endif
}

const mwinBackendOps mwinWebBackend = {
    Start,         Stop, Run,        mwinWebCreateWindow, mwinWebDestroyWindow,
    mwinWebSubmit, Now,  MapKeyCode, KeyboardLayout,      NativeHandles,
    Rumble,
};
