// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The Android accessibility adapter (record mui-0008), the component
// MAUL_UI_ANDROID_ACCESSIBILITY builds on Android: the accessibility
// tree's consumer shown to Android through maul.ui.AccessProvider
// (java/maul/ui/AccessProvider.java, which the host builds into its
// application), a provider whose virtual views are the shown nodes. The
// provider is the host view's (Maul Window's
// mwinRequestAccessibilityRoot gives it); clients' actions come back
// through a function of the host's. The header is C: the JNI's types
// pass as void*.

#ifndef MAUL_UI_ACCESS_ANDROID_H
#define MAUL_UI_ACCESS_ANDROID_H

#include "maul-ui/access.h"
#include "maul-ui/access_tree.h"
#include "maul-ui/base.h"

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    typedef struct muiAndroidAdapter muiAndroidAdapter;

    // Performs an action a client asked for, by muiPerformAccessAction or
    // its own means; true when it did. Called from the provider, on the
    // main thread.
    typedef bool (*muiAndroidActionFunction)(void* user, const muiAccessRequest* request);

    // How an adapter is made. Build it with muiDefaultAndroidAdapterDef.
    typedef struct muiAndroidAdapterDef
    {
        uint32_t cookie;
        muiAllocator allocator;
        // The most nodes its tree holds, at least 1.
        uint32_t nodes;
        // The JNIEnv* of the main thread.
        void* env;
        // The android.view.View the tree lies in (a jobject), its origin
        // the tree's; the adapter holds a global reference to it.
        void* view;
        // Pixels per Maul UI unit, above 0.
        float scale;
        muiAndroidActionFunction action;
        void* user;
    } muiAndroidAdapterDef;

    /// The default def: the C library's allocation, 4096 nodes, no
    /// JNIEnv, no view, a scale of 1, no action function.
    ///
    /// @return The def.
    /// @par Thread safety
    /// Safe from any thread.
    MUI_API muiAndroidAdapterDef muiDefaultAndroidAdapterDef(void);

    /// Makes an adapter with an empty tree, and its provider: the class
    /// maul.ui.AccessProvider is found through the view's class loader,
    /// so that any thread the JNIEnv belongs to may make it.
    ///
    /// @param def         The def, from muiDefaultAndroidAdapterDef.
    /// @param adapterOut  Receives the adapter; NULL on failure.
    /// @return `mui_success`; `mui_errorInvalid` for a NULL argument, a
    ///         def not from muiDefaultAndroidAdapterDef, a half-set
    ///         allocator, no nodes, no JNIEnv, no view, no action
    ///         function or a scale not above 0; `mui_errorCapacity` when
    ///         memory runs out; `mui_errorPlatform` when the provider's
    ///         class is not in the application or Java fails.
    /// @par Thread safety
    /// Main thread only.
    MUI_NODISCARD MUI_API muiResult muiCreateAndroidAdapter(const muiAndroidAdapterDef* def,
                                                            muiAndroidAdapter** adapterOut);

    /// Lets go of the provider, which answers nothing from then on, and
    /// destroys the adapter; NULL is ignored. Take the provider from the
    /// view first.
    ///
    /// @param adapter  The adapter.
    /// @par Thread safety
    /// Main thread only.
    MUI_API void muiDestroyAndroidAdapter(muiAndroidAdapter* adapter);

    /// Applies an update to the adapter's tree (muiAccessTree_Apply).
    ///
    /// @param adapter  The adapter.
    /// @param update   The update.
    /// @return As muiAccessTree_Apply; `mui_errorInvalid` for a NULL
    ///         adapter.
    /// @par Thread safety
    /// Main thread only.
    MUI_NODISCARD MUI_API muiResult muiAndroidAdapter_Apply(muiAndroidAdapter* adapter,
                                                            const muiAccessUpdate* update);

    /// The adapter's tree.
    ///
    /// @param adapter  The adapter.
    /// @return The tree; NULL for a NULL adapter.
    /// @par Thread safety
    /// Main thread only.
    MUI_API const muiAccessTree* muiAndroidAdapter_GetTree(const muiAndroidAdapter* adapter);

    /// Sets the pixels per unit, as the host scales its UI.
    ///
    /// @param adapter  The adapter.
    /// @param scale    The scale, above 0.
    /// @return `mui_success`; `mui_errorInvalid` for a NULL adapter or a
    ///         scale not above 0.
    /// @par Thread safety
    /// Main thread only.
    MUI_NODISCARD MUI_API muiResult muiAndroidAdapter_SetScale(muiAndroidAdapter* adapter,
                                                               float scale);

    /// The provider (a global reference to a maul.ui.AccessProvider),
    /// for the view to give as its accessibility node provider
    /// (mwinRequestAccessibilityRoot); the adapter keeps it until
    /// destroyed.
    ///
    /// @param adapter  The adapter.
    /// @return The provider (a jobject), or NULL for a NULL adapter.
    /// @par Thread safety
    /// Main thread only.
    MUI_API void* muiAndroidAdapter_GetRoot(muiAndroidAdapter* adapter);

#ifdef __cplusplus
}
#endif

#endif // MAUL_UI_ACCESS_ANDROID_H
