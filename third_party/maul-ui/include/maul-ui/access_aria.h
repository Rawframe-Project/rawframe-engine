// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The ARIA adapter, the component MAUL_UI_ARIA builds for Emscripten: the
// accessibility tree's consumer mirrored into elements of the page, which
// the browser gives its accessibility clients. The elements are built in
// an element of the host's over the canvas (Maul Window's accessibility
// host, say), each placed over what it names and invisible; clients'
// actions come back through a function of the host's.
//
// A page cannot tell whether a screen reader runs, and the elements
// cost every user, so by default nothing is built until the program
// enables the adapter or a screen reader user presses the visually
// hidden button the adapter puts in the host. Every function here is
// used on the page's main thread.
//
// An adapter is busy while one of its functions that change it runs, or
// its allocator or action function runs, from those or from its
// platform's calls: a call back into one that changes it answers
// mui_errorBusy, NULL or false, or does nothing where it returns no
// status, as the destroy does.

#ifndef MAUL_UI_ACCESS_ARIA_H
#define MAUL_UI_ACCESS_ARIA_H

#include "maul-ui/access.h"
#include "maul-ui/access_tree.h"
#include "maul-ui/base.h"

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    typedef struct muiAriaAdapter muiAriaAdapter;

    // How an adapter is made. Build it with muiDefaultAriaAdapterDef.
    typedef struct muiAriaAdapterDef
    {
        uint32_t cookie;
        uint32_t version;
        muiAllocator allocator;
        // The most nodes its tree holds, from 1 to MUI_MAX_ACCESS_NODES.
        uint32_t nodes;
        // A CSS selector of the element to build in, UTF-8,
        // NUL-terminated; read when the adapter is made. The element
        // lies over the canvas, its origin the canvas's.
        const char* host;
        // CSS pixels per Maul UI unit, above 0.
        float scale;
        // Whether building waits for muiAriaAdapter_Enable or the
        // enabling button.
        bool deferred;
        // The enabling button's label, UTF-8, NUL-terminated; read when
        // the adapter is made.
        const char* enableLabel;
        // What a client asks of a node (muiAccessActionFunction), called from
        // the page's events.
        muiAccessActionFunction action;
        void* user;
    } muiAriaAdapterDef;

// The cookie of an ARIA adapter def, which muiDefaultAriaAdapterDef sets and
// muiCreateAriaAdapter checks.
#define MUI_ARIA_ADAPTER_DEF_COOKIE 0x6D756172u

    /// The default def: the C library's allocation, 4096 nodes, no host,
    /// a scale of 1, building deferred behind a button labelled "Enable
    /// accessibility", no action function.
    ///
    /// Built in the program from the headers it includes, so it carries
    /// their version (MUI_ABI_VERSION), which muiCreateAriaAdapter checks.
    ///
    /// @return The def.
    /// @par Thread safety
    /// Safe from any thread.
    static inline muiAriaAdapterDef muiDefaultAriaAdapterDef(void)
    {
#ifdef __cplusplus
        muiAriaAdapterDef def = {};
#else
    muiAriaAdapterDef def = {0};
#endif
        def.cookie = MUI_ARIA_ADAPTER_DEF_COOKIE;
        def.version = MUI_ABI_VERSION;
        def.nodes = 4096;
        def.scale = 1.0f;
        def.deferred = true;
        def.enableLabel = "Enable accessibility";
        return def;
    }

    /// Makes an adapter with an empty tree in the host element, with the
    /// enabling button when building is deferred.
    ///
    /// @param def         The def, from muiDefaultAriaAdapterDef.
    /// @param adapterOut  Receives the adapter; NULL on failure.
    /// @return `mui_success`; `mui_errorVersion` for a def built against
    ///         headers of another binary interface (MUI_ABI_VERSION), before
    ///         the rest of the def is read; `mui_errorInvalid` for a NULL
    ///         argument, a def not from muiDefaultAriaAdapterDef, a half-set
    ///         allocator, no nodes, no host, no label, no action function or a
    ///         scale not above 0; `mui_errorPlatform` when no element matches
    ///         the host, or there is no page; `mui_errorLimit` for more than
    ///         MUI_MAX_ACCESS_NODES nodes; `mui_errorCapacity` when memory
    ///         runs out.
    /// @par Thread safety
    /// Main thread only.
    MUI_NODISCARD MUI_API muiResult muiCreateAriaAdapter(const muiAriaAdapterDef* def,
                                                         muiAriaAdapter** adapterOut);

    /// Takes the adapter's elements out of the page and destroys it; NULL
    /// is ignored.
    ///
    /// @param adapter  The adapter.
    /// @par Thread safety
    /// Main thread only.
    MUI_API void muiDestroyAriaAdapter(muiAriaAdapter* adapter);

    /// Returns how many calls the adapter refused as invalid input
    /// (`mui_errorInvalid`) or as calls back in while busy
    /// (`mui_errorBusy`): a count release builds can watch to catch a
    /// host's bugs. Queries taking it as const count nothing.
    ///
    /// @param adapter  The adapter.
    /// @return The count; 0 for a NULL adapter.
    /// @par Thread safety
    /// Main thread only.
    MUI_API uint64_t muiAriaAdapter_GetMisuse(const muiAriaAdapter* adapter);

    /// Applies an update to the adapter's tree (muiAccessTree_Apply) and,
    /// once enabled, brings the elements to it.
    ///
    /// @param adapter  The adapter.
    /// @param update   The update.
    /// @return As muiAccessTree_Apply; `mui_errorInvalid` for a NULL adapter;
    ///         `mui_errorBusy` while the adapter is busy.
    /// @par Thread safety
    /// Main thread only.
    MUI_NODISCARD MUI_API muiResult muiAriaAdapter_Apply(muiAriaAdapter* adapter,
                                                         const muiAccessUpdate* update);

    /// The adapter's tree.
    ///
    /// @param adapter  The adapter.
    /// @return The tree; NULL for a NULL adapter.
    /// @par Thread safety
    /// Main thread only.
    MUI_API const muiAccessTree* muiAriaAdapter_GetTree(const muiAriaAdapter* adapter);

    /// Sets the CSS pixels per unit, as the host scales its UI, and
    /// places the elements again.
    ///
    /// @param adapter  The adapter.
    /// @param scale    The scale, above 0.
    /// @return `mui_success`; `mui_errorInvalid` for a NULL adapter or a scale
    ///         not above 0; `mui_errorBusy` while the adapter is busy.
    /// @par Thread safety
    /// Main thread only.
    MUI_NODISCARD MUI_API muiResult muiAriaAdapter_SetScale(muiAriaAdapter* adapter, float scale);

    /// Builds the elements, if building was deferred and has not begun,
    /// and takes the enabling button away. The button does the same, and
    /// gives the program's focused node the DOM focus. Building is one way
    /// by design: deferring only spares a page the elements until someone
    /// asks for them, and once built they follow the tree until the
    /// adapter is destroyed, so there is no call to take them away.
    ///
    /// @param adapter  The adapter; NULL is ignored.
    /// @par Thread safety
    /// Main thread only.
    MUI_API void muiAriaAdapter_Enable(muiAriaAdapter* adapter);

    /// Whether the elements are built.
    ///
    /// @param adapter  The adapter.
    /// @return Whether they are; false for NULL.
    /// @par Thread safety
    /// Main thread only.
    MUI_API bool muiAriaAdapter_IsEnabled(const muiAriaAdapter* adapter);

#ifdef __cplusplus
}
#endif

#endif // MAUL_UI_ACCESS_ARIA_H
