// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Accessibility hooks.

#include "accessibility.h"

#include "maul-window/accessibility.h"

mwinResult mwinRequestAccessibilityRoot(mwinContext* context, mwinWindowId window, void* root,
                                        mwinRequestId* requestOut)
{
    uint32_t slot = 0;
    int32_t request = 0;
    mwinResult status =
        mwinBeginRequest(context, window, mwin_requestAccessibilityRoot, &slot, &request);
    if (status == mwin_success)
    {
        context->windows[slot].requests[request].value.root = root;
        mwinSubmitRequest(context, slot, request, requestOut);
    }
    return status;
}

void mwinNoteAccessibilityAsked(mwinContext* context, uint32_t slot)
{
    mwinWindow* window = &context->windows[slot];
    if (window->accessibilityAsked)
    {
        return;
    }
    window->accessibilityAsked = true;
    mwinEvent event = {.type = mwin_eventAccessibilityRequested};
    mwinPost(context, slot, &event);
}
