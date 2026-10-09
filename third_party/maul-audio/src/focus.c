// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Audio focus: the host's requests, passed to the backend, and the state
// the backend reports, kept on the context and posted when it changes.

#include "focus.h"

#include "backend.h"
#include "context.h"
#include "notify.h"

#include "maul-audio/focus.h"

void maudReportFocus(maudContext* context, maudFocus focus)
{
    if (context->focus == focus)
    {
        return;
    }
    context->focus = focus;
    maudPostNotification(context,
                         &(maudNotification){.kind = maud_notifyFocusChanged, .focus = focus});
}

maudResult maudRequestFocus(maudContext* context, maudFocusRequest request, maudDeviceRole role)
{
    if (context == nullptr || request > maud_focusBriefMixed || role > maud_roleCommunications)
    {
        return maud_errorInvalid;
    }
    if (maudIsRenderingThread(context))
    {
        maudCountMisuse(context);
        return maud_errorState;
    }
    if (context->backend->requestFocus == nullptr)
    {
        return maud_errorUnsupported;
    }
    return context->backend->requestFocus(context, request, role);
}

maudResult maudGetContextFocus(const maudContext* context, maudFocus* focusOut)
{
    if (context == nullptr || focusOut == nullptr)
    {
        return maud_errorInvalid;
    }
    *focusOut = context->focus;
    return maud_success;
}
