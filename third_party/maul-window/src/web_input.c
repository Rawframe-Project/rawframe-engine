// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Web input.

#include "web_input.h"

#include "web_js.h"

#include "maul-unicode/encoding.h"

EM_JS_DEPS(mwin_web_input, "$UTF8ToString");

// clang-format off
EM_JS(void, mwinPageAttachInput, (const mwinContext* context), {
    const state = Module.mwinWeb.get(context);
    // KeyboardEvent.code to mwinKeyCode: the enum's names are the codes.
    const codes = {KeyA: 4, KeyB: 5, KeyC: 6, KeyD: 7, KeyE: 8, KeyF: 9, KeyG: 10, KeyH: 11,
        KeyI: 12, KeyJ: 13, KeyK: 14, KeyL: 15, KeyM: 16, KeyN: 17, KeyO: 18, KeyP: 19, KeyQ: 20,
        KeyR: 21, KeyS: 22, KeyT: 23, KeyU: 24, KeyV: 25, KeyW: 26, KeyX: 27, KeyY: 28, KeyZ: 29,
        Digit1: 30, Digit2: 31, Digit3: 32, Digit4: 33, Digit5: 34, Digit6: 35, Digit7: 36,
        Digit8: 37, Digit9: 38, Digit0: 39, Enter: 40, Escape: 41, Backspace: 42, Tab: 43,
        Space: 44, Minus: 45, Equal: 46, BracketLeft: 47, BracketRight: 48, Backslash: 49,
        IntlHash: 50, Semicolon: 51, Quote: 52, Backquote: 53, Comma: 54, Period: 55, Slash: 56,
        CapsLock: 57, F1: 58, F2: 59, F3: 60, F4: 61, F5: 62, F6: 63, F7: 64, F8: 65, F9: 66,
        F10: 67, F11: 68, F12: 69, PrintScreen: 70, ScrollLock: 71, Pause: 72, Insert: 73,
        Home: 74, PageUp: 75, Delete: 76, End: 77, PageDown: 78, ArrowRight: 79, ArrowLeft: 80,
        ArrowDown: 81, ArrowUp: 82, NumLock: 83, NumpadDivide: 84, NumpadMultiply: 85,
        NumpadSubtract: 86, NumpadAdd: 87, NumpadEnter: 88, Numpad1: 89, Numpad2: 90, Numpad3: 91,
        Numpad4: 92, Numpad5: 93, Numpad6: 94, Numpad7: 95, Numpad8: 96, Numpad9: 97, Numpad0: 98,
        NumpadDecimal: 99, IntlBackslash: 100, ContextMenu: 101, NumpadEqual: 103, F13: 104,
        F14: 105, F15: 106, F16: 107, F17: 108, F18: 109, F19: 110, F20: 111, F21: 112, F22: 113,
        F23: 114, F24: 115, NumpadComma: 133, IntlRo: 135, KanaMode: 136, IntlYen: 137,
        Convert: 138, NonConvert: 139, Lang1: 144, Lang2: 145, ControlLeft: 224, ShiftLeft: 225,
        AltLeft: 226, MetaLeft: 227, ControlRight: 228, ShiftRight: 229, AltRight: 230,
        MetaRight: 231};
    const names = [];
    Object.entries(codes).forEach(([name, code]) => names[code] = name);
    const input = {codes, names, layout: null};
    state.input = input;
    // What a key types with no modifier: from the layout map, else the
    // key's own character, a letter in lower case; 0 for its name.
    input.meaning = (name, key, shift) => {
        const mapped = input.layout && input.layout.get(name);
        const text = mapped || key || "";
        if ([...text].length !== 1) {
            return 0;
        }
        return (mapped || !shift ? text : text.toLowerCase()).codePointAt(0);
    };
    const load = () => navigator.keyboard.getLayoutMap().then(map => {
        const changed = input.layout !== null;
        input.layout = map;
        if (changed) {
            state.push(19, -1);
        }
    }).catch(() => {});
    const keyboard = navigator.keyboard;
    if (keyboard && keyboard.getLayoutMap) {
        load();
    }
    // Where the Keyboard API announces new layouts.
    if (keyboard && keyboard.addEventListener) {
        keyboard.addEventListener('layoutchange', load);
        state.listeners.push(() => keyboard.removeEventListener('layoutchange', load));
    }
    const locked = () => state.canvases.forEach((entry, slot) => {
        const now = entry !== null && document.pointerLockElement === entry.canvas;
        if (entry && now !== entry.locked) {
            entry.locked = now;
            state.push(17, slot, now ? 1 : 0);
        }
    });
    document.addEventListener('pointerlockchange', locked);
    state.listeners.push(() => document.removeEventListener('pointerlockchange', locked));
});

EM_JS(void, mwinPageWatchKeys, (const mwinContext* context, uint32_t slot), {
    const state = Module.mwinWeb.get(context);
    const entry = state.canvases[slot];
    const input = state.input;
    const modifiers = e => (e.shiftKey ? 1 : 0) | (e.ctrlKey ? 2 : 0) | (e.altKey ? 4 : 0) |
                           (e.metaKey ? 8 : 0) | (e.getModifierState('CapsLock') ? 16 : 0) |
                           (e.getModifierState('NumLock') ? 32 : 0);
    const key = (e, down) => {
        const code = input.codes[e.code] || 0;
        const shortcut = ((e.ctrlKey || e.metaKey) && !e.altKey) ||
                         ['F5', 'F11', 'F12'].includes(e.code);
        // In the text field (web_text.c) keys type through the page, its
        // input events bringing the text; only Tab is kept from moving
        // the focus.
        const texting = e.target === entry.textarea;
        if (texting ? e.code === 'Tab' : !shortcut) {
            e.preventDefault();
        }
        if (code === 0 || e.isComposing || e.key === 'Process') {
            return;
        }
        state.push(10, slot, code, down ? 1 : 0, e.repeat ? 1 : 0,
                   input.meaning(e.code, e.key, e.shiftKey), 0, 0, 0, modifiers(e));
        // A character, typed alone or with AltGr (Control and Alt).
        const typed = !e.metaKey && (!e.ctrlKey || e.altKey) && [...e.key].length === 1;
        if (down && typed && !texting) {
            state.push(11, slot, e.key.codePointAt(0));
        }
    };
    const onDown = e => key(e, true);
    const onUp = e => key(e, false);
    entry.keyDown = onDown;
    entry.keyUp = onUp;
    entry.canvas.addEventListener('keydown', onDown);
    entry.canvas.addEventListener('keyup', onUp);
    entry.listeners.push(() => {
        entry.canvas.removeEventListener('keydown', onDown);
        entry.canvas.removeEventListener('keyup', onUp);
    });
});

EM_JS(void, mwinPageWatchPointer, (const mwinContext* context, uint32_t slot), {
    const state = Module.mwinWeb.get(context);
    const entry = state.canvases[slot];
    const canvas = entry.canvas;
    const push = state.push;
    const modifiers = e => (e.shiftKey ? 1 : 0) | (e.ctrlKey ? 2 : 0) | (e.altKey ? 4 : 0) |
                           (e.metaKey ? 8 : 0);
    // The DOM's buttons (left, middle, right, back, forward) as mwinMouseButton.
    const buttonOf = button => [1, 3, 2, 4, 5][button] || 0;
    const mouse = (e, phase, x, y) => {
        if (phase === 0 && document.pointerLockElement === canvas) {
            push(14, slot, 0, e.movementX, e.movementY);
            return;
        }
        push(12, slot, phase, x, y, buttonOf(e.button), 0, 0, 0, modifiers(e));
        // A button pressed or let go while another is held moves.
        if (phase === 0 && e.button >= 0) {
            const held = (e.buttons & [1, 4, 2, 8, 16][e.button]) !== 0;
            push(12, slot, held ? 3 : 4, x, y, buttonOf(e.button), 0, 0, 0, modifiers(e));
        }
    };
    const pointer = (e, phase) => {
        const box = canvas.getBoundingClientRect();
        const x = e.clientX - box.left - canvas.clientLeft;
        const y = e.clientY - box.top - canvas.clientTop;
        if (phase === 3 && e.pointerType !== 'touch') {
            canvas.setPointerCapture(e.pointerId);
        }
        if (e.pointerType === 'mouse') {
            mouse(e, phase, x, y);
        } else if (e.pointerType === 'touch') {
            // Contacts: down, move, up, cancel.
            push(15, slot, [1, -1, -1, 0, 2, 3][phase], x, y, e.pressure, 0, 0, 0, e.pointerId);
        } else if (phase !== 1 && phase !== 2) {
            // A pen's down and up are its tip's; a barrel button pressed
            // while it hovers is a pointerdown too.
            const flags = ((e.buttons & 32) !== 0 ? 1 : 0) | ((e.buttons & 1) !== 0 ? 2 : 0) |
                          ((e.buttons & 2) !== 0 ? 4 : 0);
            push(16, slot, 0, x, y, e.pressure, e.tiltX, e.tiltY, 0, flags);
        }
    };
    // Phases: move, enter, leave, down, up, cancel.
    const types = ['pointermove', 'pointerenter', 'pointerleave', 'pointerdown', 'pointerup',
                   'pointercancel'];
    const listen = (type, handler, options) => {
        canvas.addEventListener(type, handler, options);
        entry.listeners.push(() => canvas.removeEventListener(type, handler, options));
    };
    types.forEach((type, phase) => listen(type, e => pointer(e, phase)));
    listen('wheel', e => {
        e.preventDefault();
        // Pixels, lines or pages to detents.
        const unit = [100, 3, 1][e.deltaMode] || 1;
        push(13, slot, 0, e.deltaX / unit, -e.deltaY / unit);
    }, {passive: false});
    listen('contextmenu', e => e.preventDefault());
});

EM_JS(int, mwinPageLockPointer, (const mwinContext* context, uint32_t slot, bool lock), {
    const state = Module.mwinWeb.get(context);
    const canvas = state.canvases[slot].canvas;
    if ((document.pointerLockElement === canvas) === !!lock) {
        return 1;
    }
    if (!lock) {
        document.exitPointerLock();
        return 0;
    }
    if (!canvas.requestPointerLock) {
        return -1;
    }
    const failed = () => state.push(18, slot);
    // Motion without the system's acceleration where the browser can.
    const asked = canvas.requestPointerLock({unadjustedMovement: true});
    if (asked && asked.catch) {
        asked.catch(error => {
            const again = error.name === 'NotSupportedError' && canvas.requestPointerLock();
            (again && again.catch) ? again.catch(failed) : (again || failed());
        });
    }
    return 0;
});

EM_JS(void, mwinPageSetCursor, (const mwinContext* context, uint32_t slot, int shape, bool hidden), {
    const shapes = ['default', 'text', 'pointer', 'crosshair', 'move', 'ew-resize', 'ns-resize',
                    'nesw-resize', 'nwse-resize', 'not-allowed', 'wait', 'progress'];
    Module.mwinWeb.get(context).canvases[slot].canvas.style.cursor =
        hidden ? 'none' : shapes[shape];
});

EM_JS(int, mwinPageKeyMeaning, (const mwinContext* context, int code), {
    const input = Module.mwinWeb.get(context).input;
    const name = input.names[code];
    // Without a layout map, what a US layout would say.
    const fallback = /^Key[A-Z]$/.test(name) ? name[3].toLowerCase()
                   : /^Digit[0-9]$/.test(name) ? name[5]
                   : name === 'Space' ? ' ' : "";
    return name ? input.meaning(name, fallback, false) : 0;
});
// clang-format on

void mwinWebAttachInput(const mwinContext* context)
{
    mwinPageAttachInput(context);
}

void mwinWebWatchCanvas(const mwinContext* context, uint32_t slot)
{
    mwinPageWatchKeys(context, slot);
    mwinPageWatchPointer(context, slot);
}

static void Post(mwinWebPlatform* platform, uint32_t slot, mwinEvent* event, double timeMs)
{
    event->timeNs = mwinWebNanoseconds(timeMs);
    mwinPost(platform->context, slot, event);
}

static void OnKey(mwinWebPlatform* platform, uint32_t slot, const mwinWebRecord* record)
{
    mwinKeyCode code = (mwinKeyCode)record->code;
    mwinKey meaning = record->z > 0.0f ? (mwinKey)record->z : MWIN_KEY_NAMED | code;
    mwinEvent event = {.type = record->x != 0.0f ? mwin_eventKeyDown : mwin_eventKeyUp};
    event.data.key = (mwinKeyEvent){code, (mwinModifiers)record->extra, meaning, record->y != 0.0f};
    Post(platform, slot, &event, record->timeMs);
}

static void OnText(mwinWebPlatform* platform, uint32_t slot, const mwinWebRecord* record)
{
    char text[4];
    size_t length = 0;
    if (record->code < 0x20 || record->code == 0x7F ||
        muniEncodeUtf8((uint32_t)record->code, text, &length) != muni_success)
    {
        return;
    }
    mwinEvent event = {.type = mwin_eventTextInput};
    event.data.text = (mwinTextEvent){text, (uint32_t)length};
    Post(platform, slot, &event, record->timeMs);
}

static void OnMouse(mwinWebPlatform* platform, uint32_t slot, const mwinWebRecord* record)
{
    static const mwinEventType types[] = {mwin_eventCursorMoved, mwin_eventCursorEntered,
                                          mwin_eventCursorLeft, mwin_eventButtonDown,
                                          mwin_eventButtonUp};
    mwinWebWindow* window = &platform->windows[slot];
    mwinPosition position = {record->x, record->y};
    mwinMouseButton button = (mwinMouseButton)record->z;
    bool pressed = record->code == mwin_webPressed;
    bool released = record->code == mwin_webReleased;
    if ((!pressed && !released) || button == 0)
    {
        button = 0;
    }
    else if (pressed)
    {
        window->buttons |= (uint8_t)(1u << (button - 1));
        (void)mwinCountClick(&window->clicks, button, position, mwinWebNanoseconds(record->timeMs));
    }
    else
    {
        window->buttons &= (uint8_t)~(1u << (button - 1));
    }
    if ((pressed || released) && button == 0)
    {
        return;
    }
    mwinEvent event = {.type = types[record->code]};
    event.data.pointer = (mwinPointerEvent){position, (mwinModifiers)record->extra, window->buttons,
                                            button, window->clicks.clicks};
    Post(platform, slot, &event, record->timeMs);
}

static void OnTouch(mwinWebPlatform* platform, uint32_t slot, const mwinWebRecord* record)
{
    static const mwinEventType types[] = {mwin_eventTouchDown, mwin_eventTouchMoved,
                                          mwin_eventTouchUp, mwin_eventTouchCancelled};
    if (record->code < 0 || record->code > mwin_webCancel)
    {
        return;
    }
    mwinEvent event = {.type = types[record->code]};
    event.data.touch =
        (mwinTouchEvent){(uint64_t)(uint32_t)record->extra, {record->x, record->y}, record->z};
    Post(platform, slot, &event, record->timeMs);
}

// A pen: its barrel button's changes, then its tip's or where it is.
static void OnPen(mwinWebPlatform* platform, uint32_t slot, const mwinWebRecord* record)
{
    mwinWebWindow* window = &platform->windows[slot];
    mwinPenFlags flags = (mwinPenFlags)record->extra;
    mwinEvent event = {0};
    event.data.pen =
        (mwinPenEvent){{record->x, record->y}, record->z, record->w, record->v, flags, 0};
    if (((flags ^ window->penFlags) & mwin_penBarrel) != 0)
    {
        event.type =
            (flags & mwin_penBarrel) != 0 ? mwin_eventPenButtonDown : mwin_eventPenButtonUp;
        event.data.pen.button = 1;
        Post(platform, slot, &event, record->timeMs);
        event.data.pen.button = 0;
    }
    mwinPenFlags touched = (flags ^ window->penFlags) & mwin_penContact;
    window->penFlags = flags;
    event.type = touched == 0                     ? mwin_eventPenMoved
                 : (flags & mwin_penContact) != 0 ? mwin_eventPenDown
                                                  : mwin_eventPenUp;
    Post(platform, slot, &event, record->timeMs);
}

// The page's answer to a cursor mode request, if one waits.
static void AnswerLock(mwinWebPlatform* platform, uint32_t slot, bool locked, bool failed)
{
    mwinContext* context = platform->context;
    int32_t request = mwinFindActiveRequest(
        &context->windows[slot], context->limits.requestsPerWindow, mwin_requestCursorMode);
    if (request < 0)
    {
        return;
    }
    mwinCursorMode mode = context->windows[slot].requests[request].value.code;
    bool wanted = mode == mwin_cursorCaptured;
    if (!failed && locked == wanted)
    {
        platform->windows[slot].cursorMode = mode;
        mwinPageSetCursor(context, slot, platform->windows[slot].cursorShape,
                          mode != mwin_cursorVisible);
        mwinComplete(context, slot, (uint32_t)request, mwin_outcomeDone);
    }
    else if (failed)
    {
        mwinComplete(context, slot, (uint32_t)request, mwin_outcomeDenied);
    }
}

void mwinWebHandleInputRecord(mwinWebPlatform* platform, const mwinWebRecord* record)
{
    uint32_t slot = (uint32_t)record->slot;
    mwinEvent event = {0};
    switch (record->kind)
    {
    case mwin_webKey:
        OnKey(platform, slot, record);
        break;
    case mwin_webText:
        OnText(platform, slot, record);
        break;
    case mwin_webMouse:
        OnMouse(platform, slot, record);
        break;
    case mwin_webWheel:
        event.type = mwin_eventWheel;
        event.data.wheel = (mwinWheelEvent){record->x, record->y};
        Post(platform, slot, &event, record->timeMs);
        break;
    case mwin_webMotion:
        event.type = mwin_eventRawPointerDelta;
        event.data.delta = (mwinDeltaEvent){record->x, record->y};
        Post(platform, slot, &event, record->timeMs);
        break;
    case mwin_webTouch:
        OnTouch(platform, slot, record);
        break;
    case mwin_webPen:
        OnPen(platform, slot, record);
        break;
    default:
        AnswerLock(platform, slot, record->code != 0, record->kind == mwin_webLockFailed);
        break;
    }
}

int mwinWebSetCursorMode(mwinWebPlatform* platform, uint32_t slot, mwinCursorMode mode)
{
    if (mode == mwin_cursorConfined || mode == mwin_cursorConfinedHidden)
    {
        return mwin_outcomeUnsupported;
    }
    int answer = mwinPageLockPointer(platform->context, slot, mode == mwin_cursorCaptured);
    if (answer <= 0)
    {
        return answer < 0 ? mwin_outcomeUnsupported : -1;
    }
    platform->windows[slot].cursorMode = mode;
    mwinPageSetCursor(platform->context, slot, platform->windows[slot].cursorShape,
                      mode != mwin_cursorVisible);
    return mwin_outcomeDone;
}

int mwinWebSetCursorShape(mwinWebPlatform* platform, uint32_t slot, mwinCursorShape shape)
{
    mwinWebWindow* window = &platform->windows[slot];
    window->cursorShape = shape;
    mwinPageSetCursor(platform->context, slot, shape, window->cursorMode != mwin_cursorVisible);
    return mwin_outcomeDone;
}

mwinKey mwinWebMapKeyCode(const mwinContext* context, mwinKeyCode code)
{
    int meaning = mwinPageKeyMeaning(context, (int)code);
    return meaning > 0 ? (mwinKey)meaning : MWIN_KEY_NAMED | code;
}
