// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Input methods on the web.

#include "web_text.h"

#include "web_js.h"

EM_JS_DEPS(mwin_web_text, "$stringToUTF8,$lengthBytesUTF8");

// clang-format off
// The canvas's text field, made the first time it is needed.
EM_JS(void, mwinPageMakeField, (const mwinContext* context, uint32_t slot), {
    const state = Module.mwinWeb.get(context);
    const entry = state.canvases[slot];
    if (entry.textarea) {
        return;
    }
    const field = document.createElement('textarea');
    entry.textarea = field;
    field.setAttribute('autocomplete', 'off');
    field.setAttribute('autocapitalize', 'off');
    field.setAttribute('spellcheck', 'false');
    field.setAttribute('aria-hidden', 'true');
    field.style.cssText = 'position:fixed;opacity:0;border:0;padding:0;margin:0;resize:none;' +
                          'overflow:hidden;pointer-events:none;caret-color:transparent';
    document.body.appendChild(field);
    // A string for the backend to take, then its record.
    const give = (kind, text, code) => {
        state.strings.push(text);
        state.push(kind, slot, code);
    };
    const handlers = {
        keydown: entry.keyDown,
        keyup: entry.keyUp,
        focusin: entry.focusIn,
        focusout: entry.focusOut,
        compositionstart: () => entry.composing = true,
        compositionend: e => {
            entry.composing = false;
            if (e.data) {
                give(20, e.data, 0);
            }
            give(21, "", 0);
            field.value = "";
        },
        input: e => {
            if (e.isComposing || entry.composing) {
                // The field holds only the composition, and now its caret.
                const caret = Math.min(field.selectionEnd, field.value.length);
                give(21, field.value, lengthBytesUTF8(field.value.slice(0, caret)));
                return;
            }
            if (e.inputType === 'insertText' && e.data) {
                give(20, e.data, 0);
            }
            field.value = "";
        },
    };
    Object.entries(handlers).forEach(([type, handler]) => field.addEventListener(type, handler));
    entry.listeners.push(() => field.remove());
});

EM_JS(void, mwinPagePlaceField, (const mwinContext* context, uint32_t slot, float x, float y,
                         float width, float height), {
    const entry = Module.mwinWeb.get(context).canvases[slot];
    const box = entry.canvas.getBoundingClientRect();
    const style = entry.textarea.style;
    style.left = (box.left + entry.canvas.clientLeft + x) + 'px';
    style.top = (box.top + entry.canvas.clientTop + y) + 'px';
    style.width = Math.max(width, 1) + 'px';
    style.height = Math.max(height, 1) + 'px';
    // Input methods size their windows by the field's text.
    style.fontSize = Math.max(height, 1) + 'px';
});

// Moves the focus into the field when the canvas has it, or back.
EM_JS(void, mwinPageFocusField, (const mwinContext* context, uint32_t slot, bool into), {
    const entry = Module.mwinWeb.get(context).canvases[slot];
    const from = into ? entry.canvas : entry.textarea;
    if (document.activeElement === from) {
        (into ? entry.textarea : entry.canvas).focus({preventScroll: true});
    }
    if (!into) {
        entry.textarea.value = "";
    }
});

EM_JS(void, mwinPageSetPurpose, (const mwinContext* context, uint32_t slot, int purpose), {
    const modes = ['text', 'numeric', 'email', 'text', 'url'];
    Module.mwinWeb.get(context).canvases[slot].textarea.setAttribute('inputmode', modes[purpose]);
});

// Takes the string of the record being handled: its length, or -1 when
// it does not fit.
EM_JS(int, mwinPageTakeString, (const mwinContext* context, char* out, uint32_t capacity), {
    const text = Module.mwinWeb.get(context).strings.shift() || "";
    const length = lengthBytesUTF8(text);
    if (length >= capacity) {
        return -1;
    }
    stringToUTF8(text, out, capacity);
    return length;
});
// clang-format on

static void Post(mwinWebPlatform* platform, uint32_t slot, mwinEvent* event, double timeMs)
{
    event->timeNs = mwinWebNanoseconds(timeMs);
    mwinPost(platform->context, slot, event);
}

// Ends a composition the window shows.
static void EndComposition(mwinWebPlatform* platform, uint32_t slot, double timeMs)
{
    if (!platform->context->windows[slot].state.composing)
    {
        return;
    }
    mwinEvent end = {.type = mwin_eventImePreedit};
    end.data.preedit.caret = -1;
    Post(platform, slot, &end, timeMs);
}

mwinOutcome mwinWebSetTextInput(mwinWebPlatform* platform, uint32_t slot, bool enabled,
                                mwinRect caret)
{
    mwinContext* context = platform->context;
    mwinPageMakeField(context, slot);
    mwinPagePlaceField(context, slot, caret.x, caret.y, caret.width, caret.height);
    mwinPageFocusField(context, slot, enabled);
    if (!enabled)
    {
        EndComposition(platform, slot, mwinWebNow());
    }
    return mwin_outcomeDone;
}

mwinOutcome mwinWebSetVirtualKeyboard(mwinWebPlatform* platform, uint32_t slot, bool visible,
                                      mwinInputPurpose purpose)
{
    mwinContext* context = platform->context;
    mwinPageMakeField(context, slot);
    mwinPageSetPurpose(context, slot, (int)purpose);
    mwinPageFocusField(context, slot, visible);
    return mwin_outcomeDone;
}

void mwinWebHandleTextRecord(mwinWebPlatform* platform, const mwinWebRecord* record)
{
    uint32_t slot = (uint32_t)record->slot;
    uint32_t capacity = platform->context->limits.textBytesPerWindow;
    int length = mwinPageTakeString(platform->context, platform->text, capacity + 1);
    mwinEvent event = {0};
    if (length < 0)
    {
        // Past the limit: the program's text state cannot follow.
        event.type = mwin_eventInputStateReset;
        Post(platform, slot, &event, record->timeMs);
        return;
    }
    if (record->kind == mwin_webCommit)
    {
        event.type = mwin_eventTextInput;
        event.data.text = (mwinTextEvent){platform->text, (uint32_t)length};
        Post(platform, slot, &event, record->timeMs);
        return;
    }
    if (length == 0)
    {
        EndComposition(platform, slot, record->timeMs);
        return;
    }
    mwinPreeditSegment segment = {0, (uint32_t)length, mwin_preeditUnderline};
    uint32_t caret =
        (uint32_t)record->code <= (uint32_t)length ? (uint32_t)record->code : (uint32_t)length;
    event.type = mwin_eventImePreedit;
    event.data.preedit = (mwinPreeditEvent){
        platform->text, (uint32_t)length, (int32_t)caret, caret, caret, &segment, 1};
    Post(platform, slot, &event, record->timeMs);
}
