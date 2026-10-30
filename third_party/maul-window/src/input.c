// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Cursor requests, the keyboard layout and the chords the platform
// keeps, which the backend answers.

#include "maul-window/input.h"

#include "core.h"

#include <math.h>

mwinResult mwinWindow_RequestVirtualKeyboard(mwinContext* context, mwinWindowId window,
                                             bool visible, mwinInputPurpose purpose,
                                             mwinRequestId* requestOut)
{
    mwinClearOut(requestOut, sizeof *requestOut);
    if (mwinIsBusy(context))
    {
        return mwinBusy(context);
    }
    if (purpose >= MWIN_INPUT_PURPOSES)
    {
        return mwinMisuse(context);
    }
    uint32_t slot = 0;
    int32_t request = 0;
    mwinResult status =
        mwinBeginRequest(context, window, mwin_requestVirtualKeyboard, &slot, &request);
    if (status == mwin_success)
    {
        // The purpose in the low bits, the high bit set to show.
        context->windows[slot].requests[request].value.code =
            (uint8_t)(purpose | (visible ? 0x80u : 0u));
        mwinSubmitRequest(context, slot, request, requestOut);
    }
    return status;
}

mwinResult mwinWindow_RequestTextInput(mwinContext* context, mwinWindowId window, bool enabled,
                                       mwinRect caret, mwinRequestId* requestOut)
{
    mwinClearOut(requestOut, sizeof *requestOut);
    if (mwinIsBusy(context))
    {
        return mwinBusy(context);
    }
    if (!isfinite(caret.x) || !isfinite(caret.y) || !isfinite(caret.width) ||
        !isfinite(caret.height) || caret.width < 0.0f || caret.height < 0.0f)
    {
        return mwinMisuse(context);
    }
    uint32_t slot = 0;
    int32_t request = 0;
    mwinResult status = mwinBeginRequest(context, window, mwin_requestTextInput, &slot, &request);
    if (status == mwin_success)
    {
        mwinRequest* entry = &context->windows[slot].requests[request];
        entry->value.textInput.enabled = enabled;
        entry->value.textInput.caret = caret;
        mwinSubmitRequest(context, slot, request, requestOut);
    }
    return status;
}

mwinResult mwinWindow_RequestCursorMode(mwinContext* context, mwinWindowId window,
                                        mwinCursorMode mode, mwinRequestId* requestOut)
{
    mwinClearOut(requestOut, sizeof *requestOut);
    if (mwinIsBusy(context))
    {
        return mwinBusy(context);
    }
    if (mode > mwin_cursorConfinedHidden)
    {
        return mwinMisuse(context);
    }
    uint32_t slot = 0;
    int32_t request = 0;
    mwinResult status = mwinBeginRequest(context, window, mwin_requestCursorMode, &slot, &request);
    if (status == mwin_success)
    {
        context->windows[slot].requests[request].value.code = mode;
        mwinSubmitRequest(context, slot, request, requestOut);
    }
    return status;
}

mwinResult mwinWindow_RequestCursorShape(mwinContext* context, mwinWindowId window,
                                         mwinCursorShape shape, mwinRequestId* requestOut)
{
    mwinClearOut(requestOut, sizeof *requestOut);
    if (mwinIsBusy(context))
    {
        return mwinBusy(context);
    }
    if (shape > mwin_shapeProgress)
    {
        return mwinMisuse(context);
    }
    uint32_t slot = 0;
    int32_t request = 0;
    mwinResult status = mwinBeginRequest(context, window, mwin_requestCursorShape, &slot, &request);
    if (status == mwin_success)
    {
        context->windows[slot].requests[request].value.code = shape;
        mwinSubmitRequest(context, slot, request, requestOut);
    }
    return status;
}

mwinKey mwinContext_MapKeyCode(const mwinContext* context, mwinKeyCode code)
{
    if (context == nullptr || code == mwin_codeUnknown || code > mwin_codeMetaRight)
    {
        return 0;
    }
    return context->backend->mapKeyCode(context, code);
}

mwinResult mwinContext_GetKeyReach(const mwinContext* context, mwinKeyCode code,
                                   mwinModifiers modifiers, mwinKeyReach* reachOut)
{
    mwinClearOut(reachOut, sizeof *reachOut);
    if (context == nullptr || reachOut == nullptr || code == mwin_codeUnknown ||
        code > mwin_codeMetaRight)
    {
        return mwinMisuse(context);
    }
    *reachOut = context->backend->keyReach(context, code, modifiers);
    return mwin_success;
}

mwinResult mwinContext_GetKeyboardLayout(const mwinContext* context, char* bufferOut,
                                         size_t capacity, size_t* lengthOut)
{
    mwinClearOut(bufferOut, capacity);
    mwinClearOut(lengthOut, sizeof *lengthOut);
    if (context == nullptr || lengthOut == nullptr || (bufferOut == nullptr && capacity != 0))
    {
        return mwinMisuse(context);
    }
    mwinResult status = context->backend->keyboardLayout(context, bufferOut, capacity, lengthOut);
    if (status != mwin_success)
    {
        // A backend may have written part of the name before it failed.
        mwinClearOut(bufferOut, capacity);
        if (status != mwin_errorCapacity)
        {
            *lengthOut = 0;
        }
    }
    return status;
}
