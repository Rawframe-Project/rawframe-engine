// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Text input and the on-screen keyboard on the test platform: the
// requests carried out, and what they asked for kept for the test's
// readers (mwin-0038).

#include "backend_test.h"

#include "maul-window/test.h"

void mwinTestCarryOutText(mwinContext* context, uint32_t slot, const mwinRequest* request)
{
    const mwinWindow* window = &context->windows[slot];
    mwinTestText* text = &mwinTestPlatformOf(context)->texts[slot];
    if (request->kind == mwin_requestTextInput)
    {
        text->enabled = request->value.textInput.enabled;
        text->caret = request->value.textInput.caret;
        if (!text->enabled && window->state.composing)
        {
            // Leaving text input ends the composition.
            mwinEvent end = {0};
            end.type = mwin_eventImePreedit;
            end.timeNs = mwinTestPlatformOf(context)->timeNs;
            end.data.preedit.caret = -1;
            mwinPost(context, slot, &end);
        }
        return;
    }
    // The keyboard covers the lower two fifths of the window.
    mwinSize size = window->state.size;
    mwinEvent event = {0};
    event.type = mwin_eventVirtualKeyboardChanged;
    event.timeNs = mwinTestPlatformOf(context)->timeNs;
    text->keyboard = (request->value.code & 0x80u) != 0;
    text->purpose = (mwinInputPurpose)(request->value.code & 0x7Fu);
    if (text->keyboard)
    {
        event.data.rect = (mwinRect){0.0f, size.height * 0.6f, size.width, size.height * 0.4f};
    }
    mwinPost(context, slot, &event);
}

// The test platform and the window slot of a reader's arguments, or
// the reader's error.
static mwinResult FindText(const mwinContext* context, mwinWindowId window,
                           const mwinTestText** textOut)
{
    const mwinTestPlatform* platform = context != nullptr ? mwinTestPlatformOf(context) : nullptr;
    if (context == nullptr)
    {
        return mwin_errorInvalid;
    }
    if (platform == nullptr)
    {
        return mwin_errorUnsupported;
    }
    if (mwinFindWindow(context, window) == nullptr)
    {
        return mwin_errorStale;
    }
    *textOut = &platform->texts[window.index1 - 1];
    return mwin_success;
}

mwinResult mwinTestGetVirtualKeyboard(const mwinContext* context, mwinWindowId window,
                                      bool* visibleOut, mwinInputPurpose* purposeOut)
{
    const mwinTestText* text = nullptr;
    mwinResult found = visibleOut != nullptr && purposeOut != nullptr
                           ? FindText(context, window, &text)
                           : mwin_errorInvalid;
    if (found == mwin_success)
    {
        *visibleOut = text->keyboard;
        *purposeOut = text->purpose;
    }
    return found;
}

mwinResult mwinTestGetTextInput(const mwinContext* context, mwinWindowId window, bool* enabledOut,
                                mwinRect* caretOut)
{
    const mwinTestText* text = nullptr;
    mwinResult found = enabledOut != nullptr && caretOut != nullptr
                           ? FindText(context, window, &text)
                           : mwin_errorInvalid;
    if (found == mwin_success)
    {
        *enabledOut = text->enabled;
        *caretOut = text->caret;
    }
    return found;
}
