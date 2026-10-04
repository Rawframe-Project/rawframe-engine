// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The host's lifecycle: suspending a context while the application is in
// the background, hidden or asleep, and resuming it.

#include "backend.h"
#include "context.h"
#include "follow.h"

#include "maul-audio/context.h"

maudResult maudSetContextSuspended(maudContext* context, bool suspended)
{
    if (context == nullptr)
    {
        return maud_errorInvalid;
    }
    if (maudIsRenderingThread(context))
    {
        maudCountMisuse(context);
        return maud_errorState;
    }
    if (context->hostSuspended == suspended)
    {
        return maud_success;
    }
    // The streams stop before the platform is told; on resuming, the
    // platform is told first, so they run on a context that runs.
    if (suspended)
    {
        maudSuspendForHost(context, true);
    }
    if (context->backend->suspendContext != nullptr)
    {
        context->backend->suspendContext(context, suspended);
    }
    if (!suspended)
    {
        maudSuspendForHost(context, false);
    }
    return maud_success;
}
