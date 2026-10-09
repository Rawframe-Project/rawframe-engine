// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The UIAccessibility adapter (record mui-0008), the component
// MAUL_UI_UIACCESSIBILITY builds on iOS: the accessibility tree's
// consumer shown to UIKit's accessibility, an element object a shown
// node, and a container object a shown node with shown children whose
// elements are its node's element and then its children. The root's
// object is given to the view the tree lies in (Maul Window's
// mwinRequestAccessibilityRoot does that), its container; clients'
// actions come back through a function of the host's. The header is C:
// UIKit's objects pass as void*.

#ifndef MAUL_UI_ACCESS_UIKIT_H
#define MAUL_UI_ACCESS_UIKIT_H

#include "maul-ui/access.h"
#include "maul-ui/access_tree.h"
#include "maul-ui/base.h"

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    typedef struct muiUikitAdapter muiUikitAdapter;

    // Performs an action a client asked for, by muiPerformAccessAction or
    // its own means; true when it did. Called from UIKit's accessibility
    // calls, on the main thread.
    typedef bool (*muiUikitActionFunction)(void* user, const muiAccessRequest* request);

    // How an adapter is made. Build it with muiDefaultUikitAdapterDef.
    typedef struct muiUikitAdapterDef
    {
        uint32_t cookie;
        muiAllocator allocator;
        // The most nodes its tree holds, at least 1.
        uint32_t nodes;
        // The UIView the tree lies in, its origin the tree's; the
        // adapter holds a reference to it.
        void* view;
        // Points per Maul UI unit, above 0.
        float scale;
        muiUikitActionFunction action;
        void* user;
    } muiUikitAdapterDef;

    /// The default def: the C library's allocation, 4096 nodes, no view,
    /// a scale of 1, no action function.
    ///
    /// @return The def.
    /// @par Thread safety
    /// Safe from any thread.
    MUI_API muiUikitAdapterDef muiDefaultUikitAdapterDef(void);

    /// Makes an adapter with an empty tree.
    ///
    /// @param def         The def, from muiDefaultUikitAdapterDef.
    /// @param adapterOut  Receives the adapter; NULL on failure.
    /// @return `mui_success`; `mui_errorInvalid` for a NULL argument, a
    ///         def not from muiDefaultUikitAdapterDef, a half-set allocator,
    ///         no nodes, no view, no action function or a scale not above
    ///         0; `mui_errorCapacity` when memory runs out.
    /// @par Thread safety
    /// Main thread only.
    MUI_NODISCARD MUI_API muiResult muiCreateUikitAdapter(const muiUikitAdapterDef* def,
                                                          muiUikitAdapter** adapterOut);

    /// Lets go of the adapter's objects, which answer nothing from then
    /// on, and destroys it; NULL is ignored. Take its root from the view
    /// first.
    ///
    /// @param adapter  The adapter.
    /// @par Thread safety
    /// Main thread only.
    MUI_API void muiDestroyUikitAdapter(muiUikitAdapter* adapter);

    /// Applies an update to the adapter's tree (muiAccessTree_Apply).
    ///
    /// @param adapter  The adapter.
    /// @param update   The update.
    /// @return As muiAccessTree_Apply; `mui_errorInvalid` for a NULL
    ///         adapter.
    /// @par Thread safety
    /// Main thread only.
    MUI_NODISCARD MUI_API muiResult muiUikitAdapter_Apply(muiUikitAdapter* adapter,
                                                          const muiAccessUpdate* update);

    /// The adapter's tree.
    ///
    /// @param adapter  The adapter.
    /// @return The tree; NULL for a NULL adapter.
    /// @par Thread safety
    /// Main thread only.
    MUI_API const muiAccessTree* muiUikitAdapter_GetTree(const muiUikitAdapter* adapter);

    /// Sets the points per unit, as the host scales its UI.
    ///
    /// @param adapter  The adapter.
    /// @param scale    The scale, above 0.
    /// @return `mui_success`; `mui_errorInvalid` for a NULL adapter or a
    ///         scale not above 0.
    /// @par Thread safety
    /// Main thread only.
    MUI_NODISCARD MUI_API muiResult muiUikitAdapter_SetScale(muiUikitAdapter* adapter, float scale);

    /// The root's object, for the view to give as its element
    /// (mwinRequestAccessibilityRoot): its container when it has shown
    /// children, else its element. The adapter keeps it while it is the
    /// root's object; ask again after an update.
    ///
    /// @param adapter  The adapter.
    /// @return The object (a UIAccessibilityElement), or NULL for an
    ///         empty tree or a NULL adapter.
    /// @par Thread safety
    /// Main thread only.
    MUI_API void* muiUikitAdapter_GetRoot(muiUikitAdapter* adapter);

#ifdef __cplusplus
}
#endif

#endif // MAUL_UI_ACCESS_UIKIT_H
