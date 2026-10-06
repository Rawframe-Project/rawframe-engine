// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The AT-SPI adapter (record mui-0008), the component MAUL_UI_ATSPI
// builds on Linux: the accessibility tree's consumer shown to AT-SPI,
// the accessibility service of Linux desktops. An application joins the
// accessibility bus and registers its root, whose children are its
// windows; each window is an adapter owning a consumer tree, applying
// the core's updates. Clients' actions come back through a function of
// the host's.
//
// Nothing here starts a thread or waits on the bus after the
// application is made: the host polls the application's descriptor for
// reading, with its other sources, and pumps it when it is readable and
// once a frame. Every function here is used on the thread that made the
// application, which "main thread" below means.

#ifndef MAUL_UI_ACCESS_ATSPI_H
#define MAUL_UI_ACCESS_ATSPI_H

#include "maul-ui/access.h"
#include "maul-ui/access_tree.h"
#include "maul-ui/base.h"

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    typedef struct muiAtspiApp muiAtspiApp;
    typedef struct muiAtspiAdapter muiAtspiAdapter;

    // Performs an action a client asked for, by muiPerformAccessAction or
    // its own means; true when it did. Called while the application is
    // pumped.
    typedef bool (*muiAtspiActionFunction)(void* user, const muiAccessRequest* request);

    // How an application is made. Build it with muiDefaultAtspiAppDef.
    typedef struct muiAtspiAppDef
    {
        uint32_t cookie;
        muiAllocator allocator;
        // The application's name, UTF-8, NUL-terminated; copied.
        const char* name;
        // The most windows it has at once, at least 1.
        uint32_t windows;
    } muiAtspiAppDef;

    // How a window's adapter is made. Build it with
    // muiDefaultAtspiAdapterDef.
    typedef struct muiAtspiAdapterDef
    {
        uint32_t cookie;
        // The most nodes its tree holds, at least 1.
        uint32_t nodes;
        // Pixels per Maul UI unit, above 0.
        float scale;
        muiAtspiActionFunction action;
        void* user;
    } muiAtspiAdapterDef;

    /// The default def: the C library's allocation, no name and 16
    /// windows.
    ///
    /// @return The def.
    /// @par Thread safety
    /// Safe from any thread.
    MUI_API muiAtspiAppDef muiDefaultAtspiAppDef(void);

    /// Joins the accessibility bus (the address AT_SPI_BUS_ADDRESS names,
    /// else the one the session bus's org.a11y.Bus gives) and serves the
    /// application's root, asking the registry to embed it; the answer
    /// comes at a later pump. Waits for the buses' first answers, which
    /// a local socket gives at once.
    ///
    /// @param def     The def, from muiDefaultAtspiAppDef.
    /// @param appOut  Receives the application; NULL on failure.
    /// @return `mui_success`; `mui_errorInvalid` for a NULL argument, a
    ///         def not from muiDefaultAtspiAppDef, a half-set allocator,
    ///         no name or no windows; `mui_errorPlatform` when libdbus-1
    ///         or the accessibility bus cannot be reached;
    ///         `mui_errorCapacity` when memory runs out.
    /// @par Thread safety
    /// Main thread only.
    MUI_NODISCARD MUI_API muiResult muiCreateAtspiApp(const muiAtspiAppDef* def,
                                                      muiAtspiApp** appOut);

    /// Leaves the accessibility bus; NULL is ignored. Destroy its
    /// adapters first.
    ///
    /// @param app  The application.
    /// @par Thread safety
    /// Main thread only.
    MUI_API void muiDestroyAtspiApp(muiAtspiApp* app);

    /// The descriptor to poll for reading.
    ///
    /// @param app  The application.
    /// @return The descriptor; -1 for a NULL application.
    /// @par Thread safety
    /// Main thread only.
    MUI_API int muiAtspiApp_GetDescriptor(const muiAtspiApp* app);

    /// Reads what the bus has, answers clients' calls, and writes what it
    /// can, without waiting. Call it when the descriptor is readable and
    /// once a frame, as answers wait to be written.
    ///
    /// @param app  The application; NULL is ignored.
    /// @par Thread safety
    /// Main thread only.
    MUI_API void muiAtspiApp_Pump(muiAtspiApp* app);

    /// Whether the registry has embedded the application's root.
    ///
    /// @param app  The application.
    /// @return Whether it has; false for NULL.
    /// @par Thread safety
    /// Main thread only.
    MUI_API bool muiAtspiApp_IsRegistered(const muiAtspiApp* app);

    /// The default adapter def: 4096 nodes, a scale of 1, no action
    /// function.
    ///
    /// @return The def.
    /// @par Thread safety
    /// Safe from any thread.
    MUI_API muiAtspiAdapterDef muiDefaultAtspiAdapterDef(void);

    /// Adds a window to the application: an adapter with an empty tree,
    /// whose root becomes the application root's last child.
    ///
    /// @param app         The application.
    /// @param def         The def, from muiDefaultAtspiAdapterDef.
    /// @param adapterOut  Receives the adapter; NULL on failure.
    /// @return `mui_success`; `mui_errorInvalid` for a NULL argument, a
    ///         def not from muiDefaultAtspiAdapterDef, no nodes, no
    ///         action function or a scale not above 0;
    ///         `mui_errorCapacity` when the application has its windows
    ///         or memory runs out.
    /// @par Thread safety
    /// Main thread only.
    MUI_NODISCARD MUI_API muiResult muiCreateAtspiAdapter(muiAtspiApp* app,
                                                          const muiAtspiAdapterDef* def,
                                                          muiAtspiAdapter** adapterOut);

    /// Takes a window out of the application; NULL is ignored. Its nodes
    /// answer as unknown objects from then on.
    ///
    /// @param adapter  The adapter.
    /// @par Thread safety
    /// Main thread only.
    MUI_API void muiDestroyAtspiAdapter(muiAtspiAdapter* adapter);

    /// Applies an update to the adapter's tree (muiAccessTree_Apply).
    ///
    /// @param adapter  The adapter.
    /// @param update   The update.
    /// @return As muiAccessTree_Apply; `mui_errorInvalid` for a NULL
    ///         adapter.
    /// @par Thread safety
    /// Main thread only.
    MUI_NODISCARD MUI_API muiResult muiAtspiAdapter_Apply(muiAtspiAdapter* adapter,
                                                          const muiAccessUpdate* update);

    /// The adapter's tree.
    ///
    /// @param adapter  The adapter.
    /// @return The tree; NULL for a NULL adapter.
    /// @par Thread safety
    /// Main thread only.
    MUI_API const muiAccessTree* muiAtspiAdapter_GetTree(const muiAtspiAdapter* adapter);

    /// Sets the pixels per unit, as the window's scale changes.
    ///
    /// @param adapter  The adapter.
    /// @param scale    The scale, above 0.
    /// @return `mui_success`; `mui_errorInvalid` for a NULL adapter or a
    ///         scale not above 0.
    /// @par Thread safety
    /// Main thread only.
    MUI_NODISCARD MUI_API muiResult muiAtspiAdapter_SetScale(muiAtspiAdapter* adapter, float scale);

    /// Sets where the window's client area is on the screen, in pixels,
    /// for clients asking in screen coordinates. Where the window system
    /// does not tell it (Wayland), leave it at 0, 0: screen coordinates
    /// are then the window's own, as GTK gives them.
    ///
    /// @param adapter  The adapter; NULL is ignored.
    /// @param x        The left edge.
    /// @param y        The top edge.
    /// @par Thread safety
    /// Main thread only.
    MUI_API void muiAtspiAdapter_SetPlace(muiAtspiAdapter* adapter, int32_t x, int32_t y);

#ifdef __cplusplus
}
#endif

#endif // MAUL_UI_ACCESS_ATSPI_H
