// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The UI Automation adapter (record mui-0008), the component MAUL_UI_UIA
// builds on Windows: the accessibility tree's consumer shown to UI
// Automation through a provider object per node. The host hands the
// adapter's root to its window (mwinRequestAccessibilityRoot of Maul
// Window, or its own WM_GETOBJECT through muiUiaAdapter_HandleGetObject)
// and applies the core's updates through the adapter. Clients' actions
// come back through a function of the host's.
//
// An adapter lives on its window's thread, which must be in a COM
// single-threaded apartment (OleInitialize or CoInitializeEx with
// COINIT_APARTMENTTHREADED): UI Automation calls its providers there,
// as its window's messages are dispatched. "Main thread" below means
// that thread.

#ifndef MAUL_UI_ACCESS_UIA_H
#define MAUL_UI_ACCESS_UIA_H

#include "maul-ui/access.h"
#include "maul-ui/access_tree.h"
#include "maul-ui/base.h"

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    typedef struct muiUiaAdapter muiUiaAdapter;

    // Performs an action a client asked for, by muiPerformAccessAction or
    // its own means; true when it did. Called on the window's thread
    // while UI Automation waits.
    typedef bool (*muiUiaActionFunction)(void* user, const muiAccessRequest* request);

    // How an adapter is made. Build it with muiDefaultUiaAdapterDef.
    typedef struct muiUiaAdapterDef
    {
        uint32_t cookie;
        // For the adapter's tables and its tree. Provider objects come
        // from the process heap, as clients decide how long they live.
        muiAllocator allocator;
        // The most nodes the tree holds, at least 1.
        uint32_t nodes;
        // The window, an HWND.
        void* window;
        // Pixels per Maul UI unit, above 0.
        float scale;
        muiUiaActionFunction action;
        void* user;
    } muiUiaAdapterDef;

    /// The default def: the C library's allocation, 4096 nodes, a scale
    /// of 1, and no window or action function.
    ///
    /// @return The def.
    /// @par Thread safety
    /// Safe from any thread.
    MUI_API muiUiaAdapterDef muiDefaultUiaAdapterDef(void);

    /// Creates an adapter with an empty tree.
    ///
    /// @param def         The def, from muiDefaultUiaAdapterDef.
    /// @param adapterOut  Receives the adapter; NULL on failure.
    /// @return `mui_success`; `mui_errorInvalid` for a NULL argument, a
    ///         def not from muiDefaultUiaAdapterDef, a half-set allocator,
    ///         no nodes, no window, no action function or a scale not
    ///         above 0; `mui_errorPlatform` when the thread is not in a
    ///         single-threaded apartment or UI Automation cannot be
    ///         loaded; `mui_errorCapacity` when memory runs out.
    /// @par Thread safety
    /// Main thread only.
    MUI_NODISCARD MUI_API muiResult muiCreateUiaAdapter(const muiUiaAdapterDef* def,
                                                        muiUiaAdapter** adapterOut);

    /// Destroys an adapter; NULL is ignored. Every provider object it gave
    /// out answers UIA_E_ELEMENTNOTAVAILABLE from then on, and UI
    /// Automation is told to let go of each. Take the root from the
    /// window first.
    ///
    /// @param adapter  The adapter.
    /// @par Thread safety
    /// Main thread only.
    MUI_API void muiDestroyUiaAdapter(muiUiaAdapter* adapter);

    /// Applies an update to the adapter's tree (muiAccessTree_Apply);
    /// the provider objects of nodes it removes answer
    /// UIA_E_ELEMENTNOTAVAILABLE from then on.
    ///
    /// @param adapter  The adapter.
    /// @param update   The update.
    /// @return As muiAccessTree_Apply; `mui_errorInvalid` for a NULL
    ///         adapter.
    /// @par Thread safety
    /// Main thread only.
    MUI_NODISCARD MUI_API muiResult muiUiaAdapter_Apply(muiUiaAdapter* adapter,
                                                        const muiAccessUpdate* update);

    /// The adapter's tree.
    ///
    /// @param adapter  The adapter.
    /// @return The tree; NULL for a NULL adapter.
    /// @par Thread safety
    /// Main thread only.
    MUI_API const muiAccessTree* muiUiaAdapter_GetTree(const muiUiaAdapter* adapter);

    /// The root's provider, an IRawElementProviderSimple*, for
    /// mwinRequestAccessibilityRoot; it stands for whatever node is the
    /// tree's root, and lives while the adapter does. No reference is
    /// added for the caller.
    ///
    /// @param adapter  The adapter.
    /// @return The provider; NULL for a NULL adapter.
    /// @par Thread safety
    /// Main thread only.
    MUI_API void* muiUiaAdapter_GetRoot(muiUiaAdapter* adapter);

    /// Answers WM_GETOBJECT for a window procedure of the host's own:
    /// for UiaRootObjectId, gives UI Automation the root.
    ///
    /// @param adapter    The adapter.
    /// @param wParam     The message's WPARAM.
    /// @param lParam     The message's LPARAM.
    /// @param resultOut  Receives the LRESULT to return when answered.
    /// @return Whether it answered; when not, the message goes on to
    ///         DefWindowProc.
    /// @par Thread safety
    /// Main thread only.
    MUI_API bool muiUiaAdapter_HandleGetObject(muiUiaAdapter* adapter, uintptr_t wParam,
                                               intptr_t lParam, intptr_t* resultOut);

    /// Sets the pixels per unit, as the window's DPI changes.
    ///
    /// @param adapter  The adapter.
    /// @param scale    The scale, above 0.
    /// @return `mui_success`; `mui_errorInvalid` for a NULL adapter or a
    ///         scale not above 0.
    /// @par Thread safety
    /// Main thread only.
    MUI_NODISCARD MUI_API muiResult muiUiaAdapter_SetScale(muiUiaAdapter* adapter, float scale);

#ifdef __cplusplus
}
#endif

#endif // MAUL_UI_ACCESS_UIA_H
