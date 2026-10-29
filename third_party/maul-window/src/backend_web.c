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
#include "web_cursor.h"
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

// Where the platform block's parts lie, laid out with checked
// arithmetic: the platform, its windows and the text being composed.
typedef struct PlatformParts
{
    mwinLayout layout;
    size_t windows;
    size_t text;
} PlatformParts;

static PlatformParts PartsOf(const mwinLimits* limits)
{
    PlatformParts parts = {0};
    mwinLayout* layout = &parts.layout;
    (void)mwinLayoutAdd(layout, 1, sizeof(mwinWebPlatform), alignof(mwinWebPlatform));
    parts.windows =
        mwinLayoutAdd(layout, limits->windows, sizeof(mwinWebWindow), alignof(mwinWebWindow));
    parts.text = mwinLayoutAdd(layout, (size_t)limits->textBytesPerWindow + 1, 1, 1);
    return parts;
}

static size_t PlatformBytes(const mwinContext* context)
{
    return PartsOf(&context->limits).layout.size;
}

static uint64_t NowNs(void)
{
    return mwinWebNanoseconds(mwinWebNow());
}

// The screen as a monitor, in device pixels. Its HDR facts are whether
// it shows HDR: the browser tells no luminance, so the
// headroom of HDR output is unknown.
static void ReadScreen(mwinWebPlatform* platform)
{
    float screen[4];
    mwinWebScreen(screen);
    float scale = platform->scale;
    mwinMonitorInfo info = {0};
    // The page's screen, which other scripts can redefine, as counts.
    info.bounds = (mwinPixelRect){0, 0, mwinWebCount(roundf(screen[0] * scale)),
                                  mwinWebCount(roundf(screen[1] * scale))};
    info.workArea = (mwinPixelRect){0, 0, mwinWebCount(roundf(screen[2] * scale)),
                                    mwinWebCount(roundf(screen[3] * scale))};
    info.scale = scale;
    info.primary = true;
    bool high = mwinWebHighDynamicRange();
    info.hdr = (mwinHdrFacts){.known = true, .active = high, .headroom = high ? 0.0f : 1.0f};
    mwinContext* context = platform->context;
    if (platform->monitor < 0)
    {
        platform->monitor = mwinAddMonitor(context, &info, NowNs());
    }
    else if (!mwinSameMonitorInfo(&context->monitors[platform->monitor].info, &info))
    {
        mwinChangeMonitor(context, (uint32_t)platform->monitor, &info, NowNs());
    }
}

static void ReadFacts(mwinWebPlatform* platform)
{
    mwinContext* context = platform->context;
    mwinSystemFacts facts = {.textScale = 1.0f};
    // The power source sits on the page's object, which other scripts can
    // reach: anything but a tristate is unknown.
    int theme = mwinWebTheme();
    int battery = mwinWebOnBattery(context);
    facts.theme = theme >= mwin_themeUnknown && theme <= mwin_themeDark ? (mwinTheme)theme
                                                                        : mwin_themeUnknown;
    facts.reducedMotion = mwinWebReducedMotion();
    facts.onBattery =
        battery >= mwin_unknown && battery <= mwin_yes ? (mwinTristate)battery : mwin_unknown;
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
// The page's loop without Emscripten: each animation frame
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
    PlatformParts parts = PartsOf(&context->limits);
    unsigned char* block =
        parts.layout.overflow
            ? nullptr
            : mwinAllocate(&context->allocator, parts.layout.size, alignof(max_align_t));
    if (block == nullptr)
    {
        return mwin_errorCapacity;
    }
    memset(block, 0, parts.layout.size);
    mwinWebPlatform* platform = (mwinWebPlatform*)block;
    platform->windows = (mwinWebWindow*)(block + parts.windows);
    platform->text = (char*)(block + parts.text);
    platform->context = context;
    platform->monitor = -1;
    // devicePixelRatio can be redefined by other scripts, as the records
    // can be pushed.
    platform->scale = mwinWebScaleOf(mwinWebScale());
    platform->keyHost = mwinWebReadKeyHost();
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

void mwinWebHandleRecord(mwinWebPlatform* platform, const mwinWebRecord* reported)
{
    mwinContext* context = platform->context;
    mwinWebRecord repaired = *reported;
    const mwinWebRecord* record = &repaired;
    float* numbers[] = {&repaired.x, &repaired.y, &repaired.z,
                        &repaired.w, &repaired.v, &repaired.u};
    for (size_t i = 0; i < sizeof(numbers) / sizeof(numbers[0]); i++)
    {
        *numbers[i] = isfinite(*numbers[i]) ? *numbers[i] : 0.0f;
    }
    // A slot names a live window or none.
    if (repaired.slot < 0 || (uint32_t)repaired.slot >= context->limits.windows ||
        context->windows[repaired.slot].status != mwin_slotLive)
    {
        repaired.slot = -1;
    }
    uint64_t timeNs = mwinWebNanoseconds(record->timeMs);
    switch (record->kind)
    {
    case mwin_webScaled:
        if (record->x > 0.0f)
        {
            Rescale(platform, record->x, timeNs);
        }
        break;
    case mwin_webVisibility:
        PostToWindows(platform, record->code != 0 ? mwin_eventRevealed : mwin_eventOccluded,
                      timeNs);
        break;
    case mwin_webFacts:
        ReadFacts(platform);
        ReadScreen(platform);
        break;
    case mwin_webLocales:
        ReadFacts(platform);
        break;
    case mwin_webClipboardWritten:
    case mwin_webClipboardRead:
        mwinWebHandleClipboardRecord(platform, record);
        break;
    case mwin_webDrag:
    case mwin_webDropped:
        mwinWebHandleDropRecord(platform, record);
        break;
    case mwin_webLayout:
    {
        mwinEvent event = {.type = mwin_eventKeyboardLayoutChanged, .timeNs = timeNs};
        mwinPostGlobal(context, &event);
        break;
    }
    default:
        if (record->slot < 0)
        {
            break;
        }
        if (record->kind == mwin_webCommit || record->kind == mwin_webPreedit)
        {
            mwinWebHandleTextRecord(platform, record);
        }
        else if (record->kind >= mwin_webKey && record->kind <= mwin_webLockFailed)
        {
            mwinWebHandleInputRecord(platform, record);
        }
        else
        {
            mwinWebHandleWindowRecord(platform, record);
        }
        break;
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
        mwinWebHandleRecord(platform, &record);
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
    return mwinWebPadsRumble(&PlatformOf(context)->pads, slot, low, high, durationMs, NowNs());
#else
    (void)context;
    (void)slot;
    (void)low;
    (void)high;
    (void)durationMs;
    return mwin_errorUnsupported;
#endif
}

#ifdef MAUL_WINDOW_GAMEPAD
static mwinResult TriggerRumble(mwinContext* context, uint32_t slot, float left, float right,
                                uint32_t durationMs)
{
    return mwinWebPadsTriggerRumble(&PlatformOf(context)->pads, slot, left, right, durationMs,
                                    NowNs());
}
#endif

const mwinBackendOps mwinWebBackend = {
    Start,
    Stop,
    Run,
    mwinWebCreateWindow,
    mwinWebDestroyWindow,
    mwinWebSubmit,
    Now,
    MapKeyCode,
    KeyboardLayout,
    NativeHandles,
    Rumble,
    mwinWebReleaseCursor,
#ifdef MAUL_WINDOW_GAMEPAD
    TriggerRumble,
#else
    nullptr,
#endif
    nullptr,
    mwinWebKeyReach,
};
