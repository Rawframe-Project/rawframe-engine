// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The Win32 backend: the window class, DPI awareness and the pump. The
// pump never waits: it dispatches every message the thread has, which
// calls the window procedure. Win32 ties a window to the thread that
// made it, so the program runs on that thread (Main thread only).

#include "allocator.h"
#include "backend.h"
#include "core.h"
#include "win32.h"
#include "win32_dialog.h"
#include "win32_drop.h"
#include "win32_input.h"
#include "win32_output.h"
#include "win32_services.h"
#include "win32_system.h"
#include "win32_window.h"

#include <string.h>

static mwinWin32Platform* PlatformOf(const mwinContext* context)
{
    return (mwinWin32Platform*)context->backendData;
}

// Where the platform block's parts lie, laid out with checked
// arithmetic: the platform, its windows and outputs, then its UTF-16
// buffers and its byte buffers.
typedef struct PlatformParts
{
    mwinLayout layout;
    size_t windows;
    size_t outputs;
    size_t title;
    size_t imeUnits;
    size_t localeUnits;
    size_t imeAttributes;
    size_t imeBytes;
    size_t localeText;
} PlatformParts;

static PlatformParts PartsOf(const mwinLimits* limits)
{
    PlatformParts parts = {0};
    mwinLayout* layout = &parts.layout;
    (void)mwinLayoutAdd(layout, 1, sizeof(mwinWin32Platform), alignof(mwinWin32Platform));
    parts.windows =
        mwinLayoutAdd(layout, limits->windows, sizeof(mwinWin32Window), alignof(mwinWin32Window));
    parts.outputs =
        mwinLayoutAdd(layout, limits->monitors, sizeof(mwinWin32Output), alignof(mwinWin32Output));
    parts.title =
        mwinLayoutAdd(layout, (size_t)limits->titleBytes + 1, sizeof(WCHAR), alignof(WCHAR));
    parts.imeUnits =
        mwinLayoutAdd(layout, limits->textBytesPerWindow, sizeof(WCHAR), alignof(WCHAR));
    parts.localeUnits =
        mwinLayoutAdd(layout, (size_t)limits->localeBytes + 2, sizeof(WCHAR), alignof(WCHAR));
    parts.imeAttributes = mwinLayoutAdd(layout, limits->textBytesPerWindow, 1, 1);
    parts.imeBytes = mwinLayoutAdd(layout, limits->textBytesPerWindow, 1, 1);
    parts.localeText = mwinLayoutAdd(layout, limits->localeBytes, 1, 1);
    return parts;
}

static size_t PlatformBytes(const mwinContext* context)
{
    return PartsOf(&context->limits).layout.size;
}

// Makes the process per-monitor DPI aware, unless it chose an awareness
// itself, in its manifest or by a call.
static void BecomeDpiAware(void)
{
    DPI_AWARENESS awareness = GetAwarenessFromDpiAwarenessContext(GetThreadDpiAwarenessContext());
    if (awareness == DPI_AWARENESS_UNAWARE)
    {
        (void)SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    }
}

// Registers the window class, or finds it registered by an earlier
// context of the process.
static bool RegisterWindowClass(mwinWin32Platform* platform)
{
    WNDCLASSEXW windowClass = {0};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.style = CS_HREDRAW | CS_VREDRAW | CS_OWNDC;
    windowClass.lpfnWndProc = mwinWin32WindowProc;
    windowClass.hInstance = platform->instance;
    windowClass.hCursor = LoadCursorW(nullptr, (LPCWSTR)IDC_ARROW);
    windowClass.lpszClassName = MWIN_WIN32_CLASS;
    platform->windowClass = RegisterClassExW(&windowClass);
    return platform->windowClass != 0 || GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
}

static void Stop(mwinContext* context)
{
    mwinWin32Platform* platform = PlatformOf(context);
    for (uint32_t i = 0; i < context->limits.windows; i++)
    {
        if (platform->windows[i].hwnd != nullptr)
        {
            mwinWin32DestroyWindow(context, i);
        }
    }
    if (platform->windowClass != 0)
    {
        UnregisterClassW(MWIN_WIN32_CLASS, platform->instance);
    }
#ifdef MAUL_WINDOW_GAMEPAD
    mwinWin32PadsStop(&platform->pads);
#endif
    mwinWin32KeepAwake(platform, false);
    mwinWin32StopOle(platform);
    mwinRelease(&context->allocator, platform, PlatformBytes(context), alignof(max_align_t));
    context->backendData = nullptr;
}

static mwinResult Start(mwinContext* context)
{
    const mwinLimits* limits = &context->limits;
    PlatformParts parts = PartsOf(limits);
    unsigned char* block =
        parts.layout.overflow
            ? nullptr
            : mwinAllocate(&context->allocator, parts.layout.size, alignof(max_align_t));
    if (block == nullptr)
    {
        return mwin_errorCapacity;
    }
    memset(block, 0, parts.layout.size);
    mwinWin32Platform* platform = (mwinWin32Platform*)block;
    platform->windows = (mwinWin32Window*)(block + parts.windows);
    platform->outputs = (mwinWin32Output*)(block + parts.outputs);
    platform->title = (WCHAR*)(block + parts.title);
    platform->imeUnits = (WCHAR*)(block + parts.imeUnits);
    platform->localeUnits = (WCHAR*)(block + parts.localeUnits);
    platform->imeAttributes = block + parts.imeAttributes;
    platform->imeBytes = (char*)(block + parts.imeBytes);
    platform->localeText = (char*)(block + parts.localeText);
    for (uint32_t i = 0; i < limits->monitors; i++)
    {
        platform->outputs[i].monitor = -1;
    }
    platform->context = context;
    platform->instance = GetModuleHandleW(nullptr);
    context->backendData = platform;
    BecomeDpiAware();
    mwinWin32StartOle(platform);
    if (!RegisterWindowClass(platform))
    {
        Stop(context);
        return mwin_errorPlatform;
    }
    mwinWin32RefreshMonitors(platform);
    mwinWin32ReadSystem(platform);
#ifdef MAUL_WINDOW_GAMEPAD
    mwinWin32PadsStart(&platform->pads, context);
#endif
    return mwin_success;
}

static void Pump(mwinContext* context)
{
    MSG message;
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE))
    {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    mwinWin32KeepAwake(PlatformOf(context), mwinWantsAwake(context));
    mwinWin32ShowDialogs(PlatformOf(context));
#ifdef MAUL_WINDOW_GAMEPAD
    mwinWin32PadsPump(&PlatformOf(context)->pads, mwinWin32Now());
#endif
}

static mwinResult Run(mwinContext* context)
{
    return mwinRunLoop(context, Pump);
}

static uint64_t Now(const mwinContext* context)
{
    (void)context;
    return mwinWin32Now();
}

static mwinKey MapKeyCode(const mwinContext* context, mwinKeyCode code)
{
    (void)context;
    return mwinWin32MapKeyCode(code);
}

static mwinResult KeyboardLayout(const mwinContext* context, char* buffer, size_t capacity,
                                 size_t* lengthOut)
{
    (void)context;
    return mwinWin32KeyboardLayout(buffer, capacity, lengthOut);
}

static void NativeHandles(const mwinContext* context, uint32_t slot, mwinNativeHandles* out)
{
    const mwinWin32Platform* platform = PlatformOf(context);
    out->platform = mwin_platformWin32;
    out->handles.win32.hwnd = platform->windows[slot].hwnd;
    out->handles.win32.hinstance = platform->instance;
}

static mwinResult Rumble(mwinContext* context, uint32_t slot, float low, float high,
                         uint32_t durationMs)
{
#ifdef MAUL_WINDOW_GAMEPAD
    return mwinWin32PadsRumble(&PlatformOf(context)->pads, slot, low, high, durationMs,
                               mwinWin32Now());
#else
    (void)context;
    (void)slot;
    (void)low;
    (void)high;
    (void)durationMs;
    return mwin_errorUnsupported;
#endif
}

const mwinBackendOps mwinWin32Backend = {
    Start,           Stop, Run,        mwinWin32CreateWindow, mwinWin32DestroyWindow,
    mwinWin32Submit, Now,  MapKeyCode, KeyboardLayout,        NativeHandles,
    Rumble,
};
