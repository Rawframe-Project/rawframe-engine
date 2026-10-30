// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The NSAccessibility adapter, the component MAUL_UI_NSACCESSIBILITY
// builds on macOS: the accessibility tree's consumer shown to AppKit's
// accessibility, one object a shown node that answers the NSAccessibility
// protocol. The window root's object is given to the view the tree lies in
// (Maul Window's mwinWindow_RequestAccessibilityRoot does that), whose child it
// is; clients' actions come back through a function of the host's. The
// header is C: AppKit's objects pass as void*.
//
// An adapter is busy while one of its functions that change it runs, or
// its allocator or action function runs, from those or from its
// platform's calls: a call back into one that changes it answers
// mui_errorBusy, NULL or false, or does nothing where it returns no
// status, as the destroy does.

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

    // How an adapter is made. Build it with muiDefaultNsAdapterDef.
    typedef struct muiNsAdapterDef
    {
        uint32_t cookie;
        uint32_t version;
        muiAllocator allocator;
        // The most nodes its tree holds, from 1 to MUI_MAX_ACCESS_NODES.
        uint32_t nodes;
        // The NSView the tree lies in, its origin the tree's; the
        // adapter holds a reference to it.
        void* view;
        // Points per Maul UI unit, above 0.
        float scale;
        // What a client asks of a node (muiAccessActionFunction), called from
        // AppKit's accessibility calls, on the main thread.
        muiAccessActionFunction action;
        void* user;
    } muiNsAdapterDef;

// The cookie of an NSAccessibility adapter def, which muiDefaultNsAdapterDef sets and
// muiCreateNsAdapter checks.
#define MUI_NS_ADAPTER_DEF_COOKIE 0x6D756E73u

    /// The default def: the C library's allocation, 4096 nodes, no view,
    /// a scale of 1, no action function.
    ///
    /// Built in the program from the headers it includes, so it carries
    /// their version (MUI_ABI_VERSION), which muiCreateNsAdapter checks.
    ///
    /// @return The def.
    /// @par Thread safety
    /// Safe from any thread.
    static inline muiNsAdapterDef muiDefaultNsAdapterDef(void)
    {
#ifdef __cplusplus
        muiNsAdapterDef def = {};
#else
    muiNsAdapterDef def = {0};
#endif
        def.cookie = MUI_NS_ADAPTER_DEF_COOKIE;
        def.version = MUI_ABI_VERSION;
        def.nodes = 4096;
        def.scale = 1.0f;
        return def;
    }

    /// Makes an adapter with an empty tree.
    ///
    /// @param def         The def, from muiDefaultNsAdapterDef.
    /// @param adapterOut  Receives the adapter; NULL on failure.
    /// @return `mui_success`; `mui_errorVersion` for a def built against
    ///         headers of another binary interface (MUI_ABI_VERSION), before
    ///         the rest of the def is read; `mui_errorInvalid` for a NULL
    ///         argument, a def not from muiDefaultNsAdapterDef, a half-set
    ///         allocator, no nodes, no view, no action function or a scale not
    ///         above 0; `mui_errorLimit` for more than
    ///         MUI_MAX_ACCESS_NODES nodes; `mui_errorCapacity` when memory runs out.
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

    /// Returns how many calls the adapter refused as invalid input
    /// (`mui_errorInvalid`) or as calls back in while busy
    /// (`mui_errorBusy`): a count release builds can watch to catch a
    /// host's bugs. Queries taking it as const count nothing.
    ///
    /// @param adapter  The adapter.
    /// @return The count; 0 for a NULL adapter.
    /// @par Thread safety
    /// Main thread only.
    MUI_API uint64_t muiNsAdapter_GetMisuse(const muiNsAdapter* adapter);

    /// Applies an update to the adapter's tree (muiAccessTree_Apply).
    ///
    /// @param adapter  The adapter.
    /// @param update   The update.
    /// @return As muiAccessTree_Apply; `mui_errorInvalid` for a NULL adapter;
    ///         `mui_errorBusy` while the adapter is busy.
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
    /// @return `mui_success`; `mui_errorInvalid` for a NULL adapter or a scale
    ///         not above 0; `mui_errorBusy` while the adapter is busy.
    /// @par Thread safety
    /// Main thread only.
    MUI_NODISCARD MUI_API muiResult muiNsAdapter_SetScale(muiNsAdapter* adapter, float scale);

    /// The window root's object, for the view to give as its child
    /// (mwinWindow_RequestAccessibilityRoot); the adapter keeps it while the
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
