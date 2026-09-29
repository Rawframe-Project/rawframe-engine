// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The native handle bundle of a window's current surface, which the
// backend fills.

#include "maul-window/native.h"

#include "core.h"

mwinResult mwinGetNativeHandles(const mwinContext* context, mwinWindowId window,
                                mwinNativeHandles* handlesOut)
{
    if (context == nullptr || handlesOut == nullptr)
    {
        return mwin_errorInvalid;
    }
    const mwinWindow* found = mwinFindWindow(context, window);
    if (found == nullptr)
    {
        return mwin_errorStale;
    }
    if (!found->state.created || found->state.surfaceLost)
    {
        return mwin_errorState;
    }
    *handlesOut = (mwinNativeHandles){0};
    context->backend->nativeHandles(context, window.index1 - 1, handlesOut);
    handlesOut->surfaceGeneration = found->state.surfaceGeneration;
    return mwin_success;
}
