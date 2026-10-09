// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Win32 windows.

#include "win32_window.h"

#include "chrome.h"
#include "win32_accessibility.h"
#include "win32_clipboard.h"
#include "win32_dialog.h"
#include "win32_drop.h"
#include "win32_icon.h"
#include "win32_ime.h"
#include "win32_input.h"
#include "win32_output.h"
#include "win32_pointer.h"
#include "win32_services.h"
#include "win32_system.h"
#include "win32_touch_keyboard.h"

#include "maul-unicode/encoding.h"

#include <dwmapi.h>
#include <limits.h>
#include <math.h>
#include <string.h>

// The timer that runs frames while Windows moves or sizes a window.
#define SIZE_MOVE_TIMER 1

static mwinWin32Platform* PlatformOf(const mwinContext* context)
{
    return (mwinWin32Platform*)context->backendData;
}

static float ScaleOf(const mwinWin32Window* window)
{
    return mwinWin32Scale(window->dpi);
}

static void Post(mwinWin32Window* window, mwinEvent event)
{
    event.timeNs = mwinWin32Now();
    mwinPost(window->platform->context, window->slot, &event);
}

static void PostType(mwinWin32Window* window, mwinEventType type)
{
    Post(window, (mwinEvent){.type = type});
}

// The window styles of a window style and mode. Custom chrome keeps a
// caption's styles, with which Windows snaps, animates and shadows the
// window, and draws none of it.
static DWORD StyleOf(mwinWindowStyle style, bool fullscreen)
{
    if (fullscreen)
    {
        return WS_POPUP;
    }
    DWORD resizable = (style & mwin_styleResizable) != 0 ? WS_THICKFRAME | WS_MAXIMIZEBOX : 0;
    if ((style & mwin_styleCustomChrome) != 0)
    {
        return WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | resizable;
    }
    return (style & mwin_styleDecorated) != 0 ? WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | resizable
                                              : WS_POPUP | resizable;
}

// The window size that has a client area of a size, at a DPI.
static SIZE FrameSize(DWORD style, uint32_t width, uint32_t height, uint32_t dpi)
{
    RECT rect = {0, 0, (LONG)width, (LONG)height};
    AdjustWindowRectExForDpi(&rect, style, FALSE, 0, dpi);
    return (SIZE){rect.right - rect.left, rect.bottom - rect.top};
}

static uint32_t ToPixels(float logical, uint32_t dpi)
{
    long pixels = lroundf(logical * mwinWin32Scale(dpi));
    return pixels > 0 ? (uint32_t)pixels : 1u;
}

static DWORD CurrentStyle(const mwinWin32Window* window)
{
    return (DWORD)GetWindowLongPtrW(window->hwnd, GWL_STYLE);
}

// The styles whose frame surrounds the client area: none for custom
// chrome, whose client area is the whole window (WM_NCCALCSIZE).
static DWORD FramedStyle(const mwinWin32Window* window, DWORD style)
{
    return window->customChrome ? WS_POPUP : style;
}

static void PostSize(mwinWin32Window* window)
{
    float scale = ScaleOf(window);
    mwinEvent event = {.type = mwin_eventResized};
    event.data.size = (mwinSize){(float)window->width / scale, (float)window->height / scale};
    Post(window, event);
    event.type = mwin_eventPixelSizeChanged;
    event.data.pixelSize = (mwinPixelSize){window->width, window->height};
    Post(window, event);
}

static void PostMode(mwinWin32Window* window, mwinWindowMode mode)
{
    mwinEvent event = {.type = mwin_eventModeChanged};
    event.data.mode = mode;
    Post(window, event);
}

// Reports the monitor the window is on when it changed.
static void PostMonitor(mwinWin32Window* window)
{
    HMONITOR handle = MonitorFromWindow(window->hwnd, MONITOR_DEFAULTTONEAREST);
    int32_t monitor = mwinWin32MonitorOf(window->platform, handle);
    if (monitor < 0 || monitor == window->monitor)
    {
        return;
    }
    window->monitor = monitor;
    mwinEvent event = {.type = mwin_eventDisplayChanged};
    event.data.monitor = mwinMonitorIdOf(window->platform->context, (uint32_t)monitor);
    Post(window, event);
}

static const mwinWindow* CoreOf(const mwinWin32Window* window)
{
    return &window->platform->context->windows[window->slot];
}

static void OnSize(mwinWin32Window* window, WPARAM kind, LPARAM size)
{
    mwinWindowMode mode = window->fullscreen       ? mwin_modeBorderlessFullscreen
                          : kind == SIZE_MAXIMIZED ? mwin_modeMaximized
                          : kind == SIZE_MINIMIZED ? mwin_modeMinimized
                                                   : mwin_modeWindowed;
    if (mode != CoreOf(window)->state.mode)
    {
        PostMode(window, mode);
    }
    uint32_t width = LOWORD(size);
    uint32_t height = HIWORD(size);
    // A minimized window keeps the size it will come back with.
    if (kind != SIZE_MINIMIZED && (width != window->width || height != window->height))
    {
        window->width = width;
        window->height = height;
        PostSize(window);
    }
}

static bool IsPopup(const mwinWin32Window* window)
{
    return CoreOf(window)->def.kind != mwin_windowNormal;
}

// The window's owner, or nullptr: live while the window is.
static const mwinWin32Window* OwnerOf(const mwinWin32Window* window)
{
    mwinWindowId owner = CoreOf(window)->def.owner;
    return owner.index1 != 0 ? &window->platform->windows[owner.index1 - 1] : nullptr;
}

// Where a popup's corner goes on the desktop: at an offset in logical
// units of its owner from the corner of its owner's client area.
static POINT PopupOrigin(const mwinWin32Window* window, mwinPosition offset)
{
    const mwinWin32Window* owner = OwnerOf(window);
    float scale = ScaleOf(owner);
    return (POINT){owner->x + lroundf(offset.x * scale), owner->y + lroundf(offset.y * scale)};
}

// A popup's place reported against its owner, when it changed.
static void PostOffset(mwinWin32Window* window)
{
    const mwinWin32Window* owner = OwnerOf(window);
    POINT offset = {window->x - owner->x, window->y - owner->y};
    if (offset.x == window->offset.x && offset.y == window->offset.y)
    {
        return;
    }
    window->offset = offset;
    float scale = ScaleOf(owner);
    mwinEvent event = {.type = mwin_eventMoved};
    event.data.position = (mwinPosition){(float)offset.x / scale, (float)offset.y / scale};
    Post(window, event);
}

// The popups of a window that moved keep their places against it.
static void MovePopups(const mwinWin32Window* window)
{
    const mwinContext* context = window->platform->context;
    mwinWindowId id = mwinWindowIdOf(context, window->slot);
    for (uint32_t slot = 0; slot < context->limits.windows; slot++)
    {
        const mwinWin32Window* popup = &window->platform->windows[slot];
        mwinWindowId owner = context->windows[slot].def.owner;
        if (popup->hwnd != nullptr && IsPopup(popup) && owner.index1 == id.index1 &&
            owner.generation == id.generation)
        {
            SetWindowPos(popup->hwnd, nullptr, window->x + popup->offset.x,
                         window->y + popup->offset.y, 0, 0,
                         SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
        }
    }
}

static void OnMove(mwinWin32Window* window, LPARAM place)
{
    int32_t x = (int16_t)LOWORD(place);
    int32_t y = (int16_t)HIWORD(place);
    if (IsIconic(window->hwnd) || (x == window->x && y == window->y))
    {
        return;
    }
    window->x = x;
    window->y = y;
    MovePopups(window);
    if (IsPopup(window))
    {
        PostOffset(window);
    }
    else
    {
        float scale = ScaleOf(window);
        mwinEvent event = {.type = mwin_eventMoved};
        event.data.position = (mwinPosition){(float)x / scale, (float)y / scale};
        Post(window, event);
    }
    PostMonitor(window);
}

// The window moved to a monitor of another DPI: it takes the size
// Windows suggests, which keeps its logical size.
static void OnDpiChanged(mwinWin32Window* window, WPARAM dpi, const RECT* suggested)
{
    mwinSize size = CoreOf(window)->state.size;
    window->dpi = LOWORD(dpi);
    mwinEvent event = {.type = mwin_eventScaleChanged};
    event.data.scale = (mwinScaleChange){ScaleOf(window), size};
    Post(window, event);
    SetWindowPos(window->hwnd, nullptr, suggested->left, suggested->top,
                 suggested->right - suggested->left, suggested->bottom - suggested->top,
                 SWP_NOZORDER | SWP_NOACTIVATE);
}

// Bounds the window's size by the limits the program set.
static void OnMinMax(const mwinWin32Window* window, MINMAXINFO* info)
{
    DWORD style = FramedStyle(window, CurrentStyle(window));
    if (window->minimum.width > 0.0f || window->minimum.height > 0.0f)
    {
        SIZE frame = FrameSize(style, ToPixels(window->minimum.width, window->dpi),
                               ToPixels(window->minimum.height, window->dpi), window->dpi);
        info->ptMinTrackSize.x = window->minimum.width > 0.0f ? frame.cx : info->ptMinTrackSize.x;
        info->ptMinTrackSize.y = window->minimum.height > 0.0f ? frame.cy : info->ptMinTrackSize.y;
    }
    if (window->maximum.width > 0.0f || window->maximum.height > 0.0f)
    {
        SIZE frame = FrameSize(style, ToPixels(window->maximum.width, window->dpi),
                               ToPixels(window->maximum.height, window->dpi), window->dpi);
        info->ptMaxTrackSize.x = window->maximum.width > 0.0f ? frame.cx : info->ptMaxTrackSize.x;
        info->ptMaxTrackSize.y = window->maximum.height > 0.0f ? frame.cy : info->ptMaxTrackSize.y;
    }
}

// Keeps the client area to the aspect ratio as the user drags an edge.
static void OnSizing(const mwinWin32Window* window, WPARAM edge, RECT* rect)
{
    if (window->aspectWidth == 0)
    {
        return;
    }
    SIZE frame = FrameSize(FramedStyle(window, CurrentStyle(window)), 0, 0, window->dpi);
    double ratio = (double)window->aspectWidth / (double)window->aspectHeight;
    LONG width = rect->right - rect->left - frame.cx;
    LONG height = rect->bottom - rect->top - frame.cy;
    if (edge == WMSZ_LEFT || edge == WMSZ_RIGHT || edge == WMSZ_BOTTOMLEFT ||
        edge == WMSZ_BOTTOMRIGHT)
    {
        rect->bottom = rect->top + frame.cy + (LONG)lround((double)width / ratio);
    }
    else if (edge == WMSZ_TOPLEFT || edge == WMSZ_TOPRIGHT)
    {
        rect->top = rect->bottom - frame.cy - (LONG)lround((double)width / ratio);
    }
    else
    {
        rect->right = rect->left + frame.cx + (LONG)lround((double)height * ratio);
    }
}

// A custom chrome window keeps a line of the frame DWM draws, which
// keeps its shadow; the program draws over it.
static void ExtendFrame(const mwinWin32Window* window)
{
    MARGINS margins = {0, 0, window->customChrome ? 1 : 0, 0};
    (void)DwmExtendFrameIntoClientArea(window->hwnd, &margins);
}

// Custom chrome's client area is the whole window; maximized, Windows
// puts the frame it would have drawn past the monitor's edges, which the
// client area leaves out.
static bool OnCalcSize(const mwinWin32Window* window, WPARAM whole, NCCALCSIZE_PARAMS* params)
{
    if (!window->customChrome || !whole || window->fullscreen)
    {
        return false;
    }
    if (IsZoomed(window->hwnd))
    {
        int padding = GetSystemMetricsForDpi(SM_CXPADDEDBORDER, window->dpi);
        int x = GetSystemMetricsForDpi(SM_CXFRAME, window->dpi) + padding;
        int y = GetSystemMetricsForDpi(SM_CYFRAME, window->dpi) + padding;
        params->rgrc[0].left += x;
        params->rgrc[0].right -= x;
        params->rgrc[0].top += y;
        params->rgrc[0].bottom -= y;
    }
    return true;
}

// What Windows makes of a point of the client area from the program's
// hit regions. Edges a window cannot be resized from, and a maximize
// button it cannot use, are a border and the client.
static LRESULT HitTest(const mwinWin32Window* window, LPARAM place)
{
    static const LRESULT codes[] = {
        HTCLIENT,   HTCAPTION,    HTLEFT,        HTRIGHT,  HTTOP,       HTBOTTOM, HTTOPLEFT,
        HTTOPRIGHT, HTBOTTOMLEFT, HTBOTTOMRIGHT, HTCLIENT, HTMAXBUTTON, HTCLIENT,
    };
    POINT point = {(int16_t)LOWORD(place), (int16_t)HIWORD(place)};
    ScreenToClient(window->hwnd, &point);
    float scale = ScaleOf(window);
    mwinHitKind kind = mwinHitAt(CoreOf(window), (float)point.x / scale, (float)point.y / scale);
    bool sizable = (CurrentStyle(window) & WS_THICKFRAME) != 0 && !IsZoomed(window->hwnd);
    if (kind >= mwin_hitLeft && kind <= mwin_hitBottomRight && !sizable)
    {
        return HTBORDER;
    }
    if (kind == mwin_hitMaximize && (CurrentStyle(window) & WS_MAXIMIZEBOX) == 0)
    {
        return HTCLIENT;
    }
    return codes[kind];
}

// The messages about the window as a whole: true when handled, with
// the result in *result.
static bool HandleWindowMessage(mwinWin32Window* window, UINT message, WPARAM wParam, LPARAM lParam,
                                LRESULT* result)
{
    *result = 0;
    switch (message)
    {
    case WM_CLOSE:
        PostType(window, mwin_eventCloseRequested);
        return true;
    case WM_SIZE:
        OnSize(window, wParam, lParam);
        mwinWin32ClipCursor(window, GetFocus() == window->hwnd);
        return true;
    case WM_MOVE:
        OnMove(window, lParam);
        mwinWin32ClipCursor(window, GetFocus() == window->hwnd);
        return true;
    case WM_SETFOCUS:
    case WM_KILLFOCUS:
        mwinWin32ClipCursor(window, message == WM_SETFOCUS);
        PostType(window, message == WM_SETFOCUS ? mwin_eventFocusGained : mwin_eventFocusLost);
        if (message == WM_KILLFOCUS && CoreOf(window)->def.kind == mwin_windowMenu)
        {
            // A menu is dismissed when the keyboard goes elsewhere.
            PostType(window, mwin_eventCloseRequested);
        }
        return true;
    case WM_SHOWWINDOW:
        PostType(window, wParam != 0 ? mwin_eventShown : mwin_eventHidden);
        return true;
    case WM_DPICHANGED:
        OnDpiChanged(window, wParam, mwinWin32Pointer(lParam));
        return true;
    case WM_GETMINMAXINFO:
        OnMinMax(window, mwinWin32Pointer(lParam));
        return true;
    case WM_SIZING:
        OnSizing(window, wParam, mwinWin32Pointer(lParam));
        *result = TRUE;
        return true;
    case WM_ERASEBKGND:
        // The renderer draws every pixel; erasing would flicker.
        *result = 1;
        return true;
    case WM_NCCALCSIZE:
        return OnCalcSize(window, wParam, mwinWin32Pointer(lParam));
    case WM_NCHITTEST:
        *result = DefWindowProcW(window->hwnd, message, wParam, lParam);
        *result = *result == HTCLIENT ? HitTest(window, lParam) : *result;
        return true;
    case WM_GETOBJECT:
        return mwinWin32AnswerObject(window, wParam, lParam, result);
    case WM_NCACTIVATE:
        if (!window->customChrome)
        {
            return false;
        }
        // Nothing of the frame shows to be painted as the activation
        // changes.
        *result = DefWindowProcW(window->hwnd, message, wParam, -1);
        return true;
    default:
        return false;
    }
}

// Windows runs a loop of its own while the user moves or sizes a
// window; frames go on from a timer in it.
static bool HandleSizeMove(mwinWin32Window* window, UINT message, WPARAM wParam)
{
    mwinWin32Platform* platform = window->platform;
    switch (message)
    {
    case WM_ENTERSIZEMOVE:
        platform->inSizeMove = true;
        SetTimer(window->hwnd, SIZE_MOVE_TIMER, USER_TIMER_MINIMUM, nullptr);
        return true;
    case WM_EXITSIZEMOVE:
        platform->inSizeMove = false;
        KillTimer(window->hwnd, SIZE_MOVE_TIMER);
        return true;
    case WM_TIMER:
        if (wParam == SIZE_MOVE_TIMER && platform->inSizeMove)
        {
            mwinRunCriticalFrame(platform->context);
        }
        if (wParam == MWIN_WIN32_DIALOG_TIMER)
        {
            mwinWin32DialogTick(platform);
        }
        return wParam == SIZE_MOVE_TIMER || wParam == MWIN_WIN32_DIALOG_TIMER;
    default:
        return false;
    }
}

LRESULT CALLBACK mwinWin32WindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    if (message == WM_NCCREATE)
    {
        const CREATESTRUCTW* create = mwinWin32Pointer(lParam);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)create->lpCreateParams);
    }
    mwinWin32Window* window = mwinWin32Pointer(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    LRESULT result = 0;
    // Until creation returns, the window is not the program's yet.
    if (window == nullptr || window->hwnd != hwnd)
    {
        return DefWindowProcW(hwnd, message, wParam, lParam);
    }
    if (message == WM_DISPLAYCHANGE)
    {
        mwinWin32RefreshMonitors(window->platform);
    }
    if (mwinWin32IsSystemChange(message, wParam))
    {
        mwinWin32ReadSystem(window->platform);
    }
    if (HandleSizeMove(window, message, wParam) ||
        HandleWindowMessage(window, message, wParam, lParam, &result) ||
        mwinWin32HandleInput(window, message, wParam, lParam, &result) ||
        mwinWin32HandlePointer(window, message, wParam) ||
        mwinWin32HandleDropFiles(window, message, wParam) ||
        mwinWin32HandleIme(window, message, wParam, lParam, &result))
    {
        return result;
    }
    return DefWindowProcW(hwnd, message, wParam, lParam);
}

// A title as UTF-16 with its terminator, in the platform's buffer.
static const WCHAR* WideTitle(mwinWin32Platform* platform, const char* title, size_t length)
{
    size_t units = 0;
    muniTextResult result =
        muniConvertUtf8ToUtf16(title, length, (uint16_t*)platform->title,
                               platform->context->limits.titleBytes, muni_convertReplace, &units);
    platform->title[result.status == muni_success ? units : 0] = L'\0';
    return platform->title;
}

// Reads what the new window is and reports it.
static void Establish(mwinWin32Window* window)
{
    RECT client;
    POINT origin = {0, 0};
    GetClientRect(window->hwnd, &client);
    ClientToScreen(window->hwnd, &origin);
    window->width = (uint32_t)client.right;
    window->height = (uint32_t)client.bottom;
    window->x = origin.x;
    window->y = origin.y;
    PostType(window, mwin_eventWindowCreated);
    PostMonitor(window);
    mwinEvent scale = {.type = mwin_eventScaleChanged};
    scale.data.scale = (mwinScaleChange){ScaleOf(window), CoreOf(window)->def.size};
    Post(window, scale);
    PostSize(window);
    PostMode(window, mwin_modeWindowed);
    if (IsPopup(window))
    {
        window->offset = (POINT){LONG_MIN, LONG_MIN};
        PostOffset(window);
    }
}

// Shows a window: a tooltip without taking the keyboard.
static void Show(const mwinWin32Window* window, int show)
{
    bool tooltip = CoreOf(window)->def.kind == mwin_windowTooltip;
    ShowWindow(window->hwnd, tooltip ? SW_SHOWNA : show);
}

// Makes the window: a popup undecorated at its place against its
// owner, an owned window in front of its owner and off the taskbar.
static HWND Make(mwinWin32Window* window, const mwinWindow* core, DWORD style)
{
    mwinWin32Platform* platform = window->platform;
    const mwinWin32Window* owner = OwnerOf(window);
    SIZE frame = FrameSize(FramedStyle(window, style), ToPixels(core->def.size.width, window->dpi),
                           ToPixels(core->def.size.height, window->dpi), window->dpi);
    POINT origin = {CW_USEDEFAULT, CW_USEDEFAULT};
    // Topmost as it is made: a SetWindowPos right after creation was seen
    // not to take on Windows.
    DWORD topmost = (core->def.style & mwin_styleAlwaysOnTop) != 0 ? WS_EX_TOPMOST : 0;
    DWORD extended = (owner != nullptr ? 0 : WS_EX_APPWINDOW) | topmost;
    if (IsPopup(window))
    {
        origin = PopupOrigin(window, core->def.position);
        extended = WS_EX_TOOLWINDOW | topmost |
                   (core->def.kind == mwin_windowTooltip ? WS_EX_NOACTIVATE | WS_EX_TOPMOST : 0);
    }
    return CreateWindowExW(extended, MWIN_WIN32_CLASS,
                           WideTitle(platform, core->title, core->titleLength), style, origin.x,
                           origin.y, frame.cx, frame.cy, owner != nullptr ? owner->hwnd : nullptr,
                           nullptr, platform->instance, window);
}

static void EnterFullscreen(mwinWin32Window* window);

void mwinWin32CreateWindow(mwinContext* context, uint32_t slot)
{
    mwinWin32Platform* platform = PlatformOf(context);
    const mwinWindow* core = &context->windows[slot];
    mwinWin32Window* window = &platform->windows[slot];
    *window = (mwinWin32Window){.platform = platform, .slot = slot, .monitor = -1};
    window->dpi = GetDpiForSystem();
    DWORD style = IsPopup(window) ? WS_POPUP : StyleOf(core->def.style, false);
    window->customChrome = (core->def.style & mwin_styleCustomChrome) != 0 && !IsPopup(window);
    HWND hwnd = Make(window, core, style);
    int32_t request =
        mwinFindActiveRequest(core, context->limits.requestsPerWindow, mwin_requestCreate);
    if (hwnd == nullptr)
    {
        mwinComplete(context, slot, (uint32_t)request, mwin_outcomeFailed);
        return;
    }
    // Windows opened it on a monitor of its choice, maybe of another DPI.
    window->dpi = GetDpiForWindow(hwnd);
    SIZE frame = FrameSize(FramedStyle(window, style), ToPixels(core->def.size.width, window->dpi),
                           ToPixels(core->def.size.height, window->dpi), window->dpi);
    SetWindowPos(hwnd, nullptr, 0, 0, frame.cx, frame.cy,
                 SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    window->hwnd = hwnd;
    if (window->customChrome)
    {
        // Windows sized the frame before the window was the program's.
        ExtendFrame(window);
        SetWindowPos(hwnd, nullptr, 0, 0, frame.cx, frame.cy,
                     SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
    }
    mwinWin32AttachDrop(window);
    mwinWin32StartIme(window);
    mwinWin32ApplyTheme(window);
    Establish(window);
    if (core->def.mode == mwin_modeBorderlessFullscreen)
    {
        EnterFullscreen(window);
    }
    static const int shows[] = {SW_SHOWNORMAL, SW_SHOWNORMAL, SW_SHOWMINIMIZED, SW_SHOWMAXIMIZED};
    if (core->def.visible)
    {
        Show(window, shows[core->def.mode]);
    }
    mwinComplete(context, slot, (uint32_t)request, mwin_outcomeDone);
}

void mwinWin32DestroyWindow(mwinContext* context, uint32_t slot)
{
    mwinWin32Window* window = &PlatformOf(context)->windows[slot];
    HWND hwnd = window->hwnd;
    if (hwnd != nullptr && GetFocus() == hwnd && window->cursorMode != mwin_cursorVisible)
    {
        mwinWin32ClipCursor(window, false);
    }
    if (hwnd != nullptr)
    {
        mwinWin32DetachDrop(window);
        mwinWin32DetachPane(window);
        mwinWin32ForgetObject(window);
    }
    // The window's last messages find no window of the program's; its
    // icons go after it, which shows them until then.
    mwinWin32Window icons = {.icons = {window->icons[0], window->icons[1]}};
    *window = (mwinWin32Window){.monitor = -1};
    if (hwnd != nullptr)
    {
        DestroyWindow(hwnd);
    }
    mwinWin32ReleaseIcons(&icons);
}

// A popup covering the window's monitor, its placement kept.
static void EnterFullscreen(mwinWin32Window* window)
{
    if (window->fullscreen)
    {
        return;
    }
    window->restore.length = sizeof(window->restore);
    GetWindowPlacement(window->hwnd, &window->restore);
    MONITORINFO monitor = {.cbSize = sizeof(monitor)};
    GetMonitorInfoW(MonitorFromWindow(window->hwnd, MONITOR_DEFAULTTONEAREST), &monitor);
    window->fullscreen = true;
    DWORD visible = CurrentStyle(window) & WS_VISIBLE;
    SetWindowLongPtrW(window->hwnd, GWL_STYLE, (LONG_PTR)(WS_POPUP | visible));
    RECT bounds = monitor.rcMonitor;
    SetWindowPos(window->hwnd, HWND_TOP, bounds.left, bounds.top, bounds.right - bounds.left,
                 bounds.bottom - bounds.top, SWP_FRAMECHANGED | SWP_NOOWNERZORDER);
    PostMode(window, mwin_modeBorderlessFullscreen);
}

static void LeaveFullscreen(mwinWin32Window* window)
{
    if (!window->fullscreen)
    {
        return;
    }
    window->fullscreen = false;
    DWORD visible = CurrentStyle(window) & WS_VISIBLE;
    SetWindowLongPtrW(window->hwnd, GWL_STYLE,
                      (LONG_PTR)(StyleOf(CoreOf(window)->state.style, false) | visible));
    SetWindowPlacement(window->hwnd, &window->restore);
    SetWindowPos(window->hwnd, nullptr, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED | SWP_NOACTIVATE);
    PostMode(window, mwin_modeWindowed);
}

static mwinOutcome SetMode(mwinWin32Window* window, mwinWindowMode mode)
{
    if (mode == mwin_modeBorderlessFullscreen)
    {
        if (IsZoomed(window->hwnd) || IsIconic(window->hwnd))
        {
            ShowWindow(window->hwnd, SW_RESTORE);
        }
        EnterFullscreen(window);
        return mwin_outcomeDone;
    }
    LeaveFullscreen(window);
    static const int shows[] = {SW_RESTORE, SW_RESTORE, SW_MINIMIZE, SW_MAXIMIZE};
    ShowWindow(window->hwnd, shows[mode]);
    return mwin_outcomeDone;
}

// Sizes the client area, a windowed window's only.
static mwinOutcome SetSize(mwinWin32Window* window, mwinSize size)
{
    if (window->fullscreen || IsZoomed(window->hwnd) || IsIconic(window->hwnd))
    {
        return mwin_outcomeDenied;
    }
    SIZE frame =
        FrameSize(FramedStyle(window, CurrentStyle(window)), ToPixels(size.width, window->dpi),
                  ToPixels(size.height, window->dpi), window->dpi);
    SetWindowPos(window->hwnd, nullptr, 0, 0, frame.cx, frame.cy,
                 SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    return mwin_outcomeDone;
}

// Places the client area's corner on the desktop.
static mwinOutcome SetPosition(mwinWin32Window* window, mwinPosition position)
{
    if (window->fullscreen || IsZoomed(window->hwnd))
    {
        return mwin_outcomeDenied;
    }
    if (IsPopup(window))
    {
        POINT origin = PopupOrigin(window, position);
        SetWindowPos(window->hwnd, nullptr, origin.x, origin.y, 0, 0,
                     SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
        return mwin_outcomeDone;
    }
    float scale = ScaleOf(window);
    RECT rect = {0, 0, 0, 0};
    AdjustWindowRectExForDpi(&rect, FramedStyle(window, CurrentStyle(window)), FALSE, 0,
                             window->dpi);
    SetWindowPos(window->hwnd, nullptr, (int)lroundf(position.x * scale) + rect.left,
                 (int)lroundf(position.y * scale) + rect.top, 0, 0,
                 SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    return mwin_outcomeDone;
}

// Changes the window's styles, keeping its client area.
static mwinOutcome SetStyle(mwinWin32Window* window, mwinWindowStyle style)
{
    if (!window->fullscreen)
    {
        DWORD visible = CurrentStyle(window) & (WS_VISIBLE | WS_MAXIMIZE | WS_MINIMIZE);
        DWORD next = StyleOf(style, false);
        window->customChrome = (style & mwin_styleCustomChrome) != 0 && !IsPopup(window);
        ExtendFrame(window);
        SetWindowLongPtrW(window->hwnd, GWL_STYLE, (LONG_PTR)(next | visible));
        SIZE frame =
            FrameSize(FramedStyle(window, next), window->width, window->height, window->dpi);
        SetWindowPos(window->hwnd, nullptr, 0, 0, frame.cx, frame.cy,
                     SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
    }
    SetWindowPos(window->hwnd, (style & mwin_styleAlwaysOnTop) != 0 ? HWND_TOPMOST : HWND_NOTOPMOST,
                 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    return mwin_outcomeDone;
}

static mwinOutcome SetOpacity(const mwinWin32Window* window, float opacity)
{
    LONG_PTR extended = GetWindowLongPtrW(window->hwnd, GWL_EXSTYLE);
    if (opacity >= 1.0f)
    {
        SetWindowLongPtrW(window->hwnd, GWL_EXSTYLE, extended & ~(LONG_PTR)WS_EX_LAYERED);
        return mwin_outcomeDone;
    }
    SetWindowLongPtrW(window->hwnd, GWL_EXSTYLE, extended | WS_EX_LAYERED);
    BYTE alpha = (BYTE)lroundf(opacity * 255.0f);
    return SetLayeredWindowAttributes(window->hwnd, 0, alpha, LWA_ALPHA) ? mwin_outcomeDone
                                                                         : mwin_outcomeFailed;
}

// Applies new size bounds at once, by asking for the size the window
// has.
static mwinOutcome Rebound(mwinWin32Window* window)
{
    RECT rect;
    GetWindowRect(window->hwnd, &rect);
    SetWindowPos(window->hwnd, nullptr, 0, 0, rect.right - rect.left, rect.bottom - rect.top,
                 SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    return mwin_outcomeDone;
}

// Carries out a request now: its outcome, or -1 when it is answered
// later.
static int CarryOut(mwinWin32Window* window, mwinWindow* core, uint32_t index)
{
    const mwinRequest* request = &core->requests[index];
    switch (request->kind)
    {
    case mwin_requestTitle:
        SetWindowTextW(window->hwnd,
                       WideTitle(window->platform, core->pendingTitle, core->pendingTitleLength));
        memmove(core->title, core->pendingTitle, core->pendingTitleLength);
        core->titleLength = core->pendingTitleLength;
        return mwin_outcomeDone;
    case mwin_requestSize:
        return SetSize(window, request->value.size);
    case mwin_requestPosition:
        return SetPosition(window, request->value.position);
    case mwin_requestMode:
        return SetMode(window, request->value.mode);
    case mwin_requestVisible:
        if (request->value.visible)
        {
            Show(window, SW_SHOW);
        }
        else
        {
            ShowWindow(window->hwnd, SW_HIDE);
        }
        return mwin_outcomeDone;
    case mwin_requestFocus:
        // Windows lets a process take the foreground only in some cases.
        return SetForegroundWindow(window->hwnd) ? mwin_outcomeDone : mwin_outcomeDenied;
    case mwin_requestSizeLimits:
        window->minimum = request->value.limits.minimum;
        window->maximum = request->value.limits.maximum;
        return Rebound(window);
    case mwin_requestAspectRatio:
        window->aspectWidth = request->value.aspect.width;
        window->aspectHeight = request->value.aspect.height;
        return mwin_outcomeDone;
    case mwin_requestStyle:
        return SetStyle(window, request->value.code);
    case mwin_requestOpacity:
        return SetOpacity(window, request->value.opacity);
    case mwin_requestCursorMode:
        return mwinWin32SetCursorMode(window, request->value.code);
    case mwin_requestCursorShape:
        return mwinWin32SetCursorShape(window, request->value.code);
    case mwin_requestCursorImage:
        return mwinWin32SetCursorImage(window, request->value.cursor);
    case mwin_requestTextInput:
        return mwinWin32SetTextInput(window, request->value.textInput.enabled,
                                     request->value.textInput.caret);
    case mwin_requestClipboardWrite:
    case mwin_requestClipboardWriteData:
        return mwinWin32WriteClipboard(window);
    case mwin_requestClipboardRead:
        return mwinWin32ReadClipboard(window);
    case mwin_requestClipboardReadData:
        return mwinWin32ReadClipboardData(window, request);
    case mwin_requestOpenUrl:
        return mwinWin32OpenUrl(window, request);
    case mwin_requestRevealFile:
        return mwinWin32RevealFile(request);
    case mwin_requestKeepAwake:
        // The pump keeps the display awake from the windows' state.
        return mwin_outcomeDone;
    case mwin_requestFileDialog:
        return mwinWin32AskDialog(window);
    case mwin_requestIcon:
        return mwinWin32SetIcon(window, request);
    case mwin_requestHitRegions:
        // WM_NCHITTEST reads them.
        return mwin_outcomeDone;
    case mwin_requestAccessibilityRoot:
        // WM_GETOBJECT reads it (win32_accessibility.c).
        return mwin_outcomeDone;
    case mwin_requestVirtualKeyboard:
        // The purpose in the low bits, the high bit set to show.
        return mwinWin32SetTouchKeyboard(window, (request->value.code & 0x80u) != 0,
                                         (mwinInputPurpose)(request->value.code & 0x7Fu));
    default:
        return mwin_outcomeUnsupported;
    }
}

void mwinWin32Submit(mwinContext* context, uint32_t slot, uint32_t request)
{
    mwinWin32Platform* platform = PlatformOf(context);
    int outcome = CarryOut(&platform->windows[slot], &context->windows[slot], request);
    if (outcome >= 0)
    {
        mwinComplete(context, slot, request, (mwinOutcome)outcome);
    }
}
