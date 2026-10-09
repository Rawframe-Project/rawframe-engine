// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The NSAccessibility adapter (record mui-0008), the component
// MAUL_UI_NSACCESSIBILITY builds on macOS: the accessibility tree's
// consumer shown to AppKit's accessibility, one object a shown node that
// answers the NSAccessibility protocol. The window root's object is
// given to the view the tree lies in (Maul Window's
// mwinRequestAccessibilityRoot does that), whose child it is; clients'
// actions come back through a function of the host's. The header is C:
// AppKit's objects pass as void*.

#ifndef MAUL_UI_ACCESS_NS_H
#define MAUL_UI_ACCESS_NS_H

#include "maul-ui/access.h"
#include "maul-ui/access_tree.h"
#include "maul-ui/base.h"

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    typedef struct muiNsAdapter muiNsAdapter;

    // Performs an action a client asked for, by muiPerformAccessAction or
    // its own means; true when it did. Called from AppKit's accessibility
    // calls, on the main thread.
    typedef bool (*muiNsActionFunction)(void* user, const muiAccessRequest* request);

    // How an adapter is made. Build it with muiDefaultNsAdapterDef.
    typedef struct muiNsAdapterDef
    {
        uint32_t cookie;
        muiAllocator allocator;
        // The most nodes its tree holds, at least 1.
        uint32_t nodes;
        // The NSView the tree lies in, its origin the tree's; the
        // adapter holds a reference to it.
        void* view;
        // Points per Maul UI unit, above 0.
        float scale;
        muiNsActionFunction action;
        void* user;
    } muiNsAdapterDef;

    /// The default def: the C library's allocation, 4096 nodes, no view,
    /// a scale of 1, no action function.
    ///
    /// @return The def.
    /// @par Thread safety
    /// Safe from any thread.
    MUI_API muiNsAdapterDef muiDefaultNsAdapterDef(void);

    /// Makes an adapter with an empty tree.
    ///
    /// @param def         The def, from muiDefaultNsAdapterDef.
    /// @param adapterOut  Receives the adapter; NULL on failure.
    /// @return `mui_success`; `mui_errorInvalid` for a NULL argument, a
    ///         def not from muiDefaultNsAdapterDef, a half-set allocator,
    ///         no nodes, no view, no action function or a scale not above
    ///         0; `mui_errorCapacity` when memory runs out.
    /// @par Thread safety
    /// Main thread only.
    MUI_NODISCARD MUI_API muiResult muiCreateNsAdapter(const muiNsAdapterDef* def,
                                                       muiNsAdapter** adapterOut);

    /// Lets go of the adapter's objects, which answer nothing from then
    /// on, and destroys it; NULL is ignored. Take its root from the view
    /// first.
    ///
    /// @param adapter  The adapter.
    /// @par Thread safety
    /// Main thread only.
    MUI_API void muiDestroyNsAdapter(muiNsAdapter* adapter);

    /// Applies an update to the adapter's tree (muiAccessTree_Apply).
    ///
    /// @param adapter  The adapter.
    /// @param update   The update.
    /// @return As muiAccessTree_Apply; `mui_errorInvalid` for a NULL
    ///         adapter.
    /// @par Thread safety
    /// Main thread only.
    MUI_NODISCARD MUI_API muiResult muiNsAdapter_Apply(muiNsAdapter* adapter,
                                                       const muiAccessUpdate* update);

    /// The adapter's tree.
    ///
    /// @param adapter  The adapter.
    /// @return The tree; NULL for a NULL adapter.
    /// @par Thread safety
    /// Main thread only.
    MUI_API const muiAccessTree* muiNsAdapter_GetTree(const muiNsAdapter* adapter);

    /// Sets the points per unit, as the host scales its UI.
    ///
    /// @param adapter  The adapter.
    /// @param scale    The scale, above 0.
    /// @return `mui_success`; `mui_errorInvalid` for a NULL adapter or a
    ///         scale not above 0.
    /// @par Thread safety
    /// Main thread only.
    MUI_NODISCARD MUI_API muiResult muiNsAdapter_SetScale(muiNsAdapter* adapter, float scale);

    /// The window root's object, for the view to give as its child
    /// (mwinRequestAccessibilityRoot); the adapter keeps it while the
    /// root is the tree's.
    ///
    /// @param adapter  The adapter.
    /// @return The object (an NSAccessibilityElement), or NULL for an
    ///         empty tree or a NULL adapter.
    /// @par Thread safety
    /// Main thread only.
    MUI_API void* muiNsAdapter_GetRoot(muiNsAdapter* adapter);

#ifdef __cplusplus
}
#endif

#endif // MAUL_UI_ACCESS_NS_H
