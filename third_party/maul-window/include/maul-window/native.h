// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The native handles a GPU layer needs to create a surface for a window,
// one bundle per surface generation. This library includes no graphics
// API header: the handles are the window system's own, as opaque
// pointers, and the GPU layer turns them into its surface. A lost
// surface ends a generation; the window's id stays, and the bundle of
// the next generation arrives with mwin_eventSurfaceRestored.

#ifndef MAUL_WINDOW_NATIVE_H
#define MAUL_WINDOW_NATIVE_H

#include "maul-window/window.h"

#ifdef __cplusplus
extern "C"
{
#endif

    // Which window system a bundle's handles belong to.
    typedef uint8_t mwinPlatform;

    enum
    {
        mwin_platformNone = 0,
        mwin_platformWin32 = 1,
        mwin_platformWayland = 2,
        mwin_platformX11 = 3,
        mwin_platformAndroid = 4,
        mwin_platformMacOS = 5,
        mwin_platformIOS = 6,
        mwin_platformWeb = 7,
        // The test backend, whose handles point at nothing.
        mwin_platformTest = 8,
    };

    // A window's native handles for one surface generation.
    typedef struct mwinNativeHandles
    {
        mwinPlatform platform;
        uint32_t surfaceGeneration;
        union
        {
            // HWND and HINSTANCE.
            struct
            {
                void* hwnd;
                void* hinstance;
            } win32;
            // struct wl_display* and struct wl_surface*.
            struct
            {
                void* display;
                void* surface;
            } wayland;
            // xcb_connection_t* and xcb_window_t.
            struct
            {
                void* connection;
                uint32_t window;
            } x11;
            // ANativeWindow*, the ANativeActivity* it belongs to,
            // through which the program reaches Java and its assets, and
            // the jobject of the activity's android.view.View, a global
            // reference, which hosts the accessibility root
            // (maul-window/accessibility.h). All three change with the
            // activity.
            struct
            {
                void* window;
                void* activity;
                void* view;
            } android;
            // The NSView or UIView, and its CAMetalLayer.
            struct
            {
                void* view;
                void* layer;
            } apple;
            // The canvas's CSS selector, and that of the element over it
            // for the program's accessibility elements
            // (maul-window/accessibility.h), UTF-8, not NUL-terminated.
            struct
            {
                const char* selector;
                uint32_t selectorLength;
                const char* accessibility;
                uint32_t accessibilityLength;
            } web;
        } handles;
    } mwinNativeHandles;

    /// Reads a window's native handles for its current surface.
    ///
    /// @param context     The context.
    /// @param window      The window.
    /// @param handlesOut  Receives the bundle.
    /// @return `mwin_success`; `mwin_errorState` while the window has no
    ///         surface (before mwin_eventWindowCreated, or between a lost
    ///         and a restored surface); `mwin_errorStale` for a window that
    ///         no longer exists; `mwin_errorInvalid` for a NULL argument.
    /// @par Thread safety
    /// Main thread only.
    MWIN_NODISCARD MWIN_API mwinResult mwinGetNativeHandles(const mwinContext* context,
                                                            mwinWindowId window,
                                                            mwinNativeHandles* handlesOut);

#ifdef __cplusplus
}
#endif

#endif // MAUL_WINDOW_NATIVE_H
