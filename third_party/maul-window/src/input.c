// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Cursor requests and the keyboard layout, which the backend answers.

#include "maul-window/input.h"

#include "core.h"

#include <math.h>

mwinResult mwinRequestVirtualKeyboard(mwinContext* context, mwinWindowId window, bool visible,
                                      mwinInputPurpose purpose, mwinRequestId* requestOut)
{
    if (purpose > mwin_purposeUrl)
    {
        return mwin_errorInvalid;
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

mwinResult mwinRequestTextInput(mwinContext* context, mwinWindowId window, bool enabled,
                                mwinRect caret, mwinRequestId* requestOut)
{
    if (!isfinite(caret.x) || !isfinite(caret.y) || !isfinite(caret.width) ||
        !isfinite(caret.height) || caret.width < 0.0f || caret.height < 0.0f)
    {
        return mwin_errorInvalid;
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

mwinResult mwinRequestCursorMode(mwinContext* context, mwinWindowId window, mwinCursorMode mode,
                                 mwinRequestId* requestOut)
{
    if (mode > mwin_cursorConfinedHidden)
    {
        return mwin_errorInvalid;
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

mwinResult mwinRequestCursorShape(mwinContext* context, mwinWindowId window, mwinCursorShape shape,
                                  mwinRequestId* requestOut)
{
    if (shape > mwin_shapeProgress)
    {
        return mwin_errorInvalid;
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

mwinKey mwinMapKeyCode(const mwinContext* context, mwinKeyCode code)
{
    if (context == nullptr || code == mwin_codeUnknown || code > mwin_codeMetaRight)
    {
        return 0;
    }
    return context->backend->mapKeyCode(context, code);
}

mwinResult mwinGetKeyboardLayout(const mwinContext* context, char* buffer, size_t capacity,
                                 size_t* lengthOut)
{
    if (context == nullptr || lengthOut == nullptr || (buffer == nullptr && capacity != 0))
    {
        return mwin_errorInvalid;
    }
    return context->backend->keyboardLayout(context, buffer, capacity, lengthOut);
}
