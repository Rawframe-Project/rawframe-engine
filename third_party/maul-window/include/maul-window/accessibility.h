// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The hooks a program's accessibility adapters need from its windows.
// The library knows nothing of the tree. It hands the platform the root
// the program implements, tells the program when a client first asks for
// a window's tree, and on the web keeps an element next to the canvas
// for the program's ARIA elements (its selector is in mwinNativeHandles).
//
// On Linux, AT-SPI is a service of the application on the session's
// accessibility bus, and asks nothing of the window system. Its adapter
// takes the window's place, size and focus from the window's state and
// events.

#ifndef MAUL_WINDOW_ACCESSIBILITY_H
#define MAUL_WINDOW_ACCESSIBILITY_H

#include "maul-window/window.h"

#ifdef __cplusplus
extern "C"
{
#endif

    /// Hands the platform's accessibility clients the root of the
    /// window's tree, which the program's adapter implements, or no root.
    ///
    /// On Win32 the root is an IRawElementProviderSimple*. The window
    /// answers WM_GETOBJECT for UiaRootObjectId with it, and UI
    /// Automation takes references of its own. The provider must stay
    /// valid while it is the window's root. When the window is destroyed,
    /// UI Automation is told to let go of it. X11, Wayland and the web
    /// take no root and answer mwin_outcomeUnsupported.
    ///
    /// @param context    The context.
    /// @param window     The window.
    /// @param root       The root, or NULL for none.
    /// @param requestOut Receives the request's id. May be NULL.
    /// @return `mwin_success`; `mwin_errorCapacity` when the window has its
    ///         limit of requests in flight; `mwin_errorStale` for a window
    ///         that no longer exists; `mwin_errorInvalid` for a NULL
    ///         context.
    /// @par Thread safety
    /// Main thread only.
    MWIN_NODISCARD MWIN_API mwinResult mwinRequestAccessibilityRoot(mwinContext* context,
                                                                    mwinWindowId window, void* root,
                                                                    mwinRequestId* requestOut);

#ifdef __cplusplus
}
#endif

#endif // MAUL_WINDOW_ACCESSIBILITY_H
