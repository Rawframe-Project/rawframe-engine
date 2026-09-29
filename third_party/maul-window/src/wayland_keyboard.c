// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The Wayland keyboard.

#include "wayland_keyboard.h"

#include "evdev.h"
#include "xkb_keyboard.h"

#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

// The most repeats one pump posts after a stall; the rest are dropped.
#define MAX_REPEATS_PER_PUMP 8

static void PostLayoutChange(mwinWaylandPlatform* platform)
{
    mwinEvent event = {0};
    event.type = mwin_eventKeyboardLayoutChanged;
    event.timeNs = mwinMonotonicNow();
    mwinPostGlobal(platform->context, &event);
}

static void OnKeymap(void* data, struct wl_keyboard* object, uint32_t format, int32_t fd,
                     uint32_t size)
{
    (void)object;
    mwinWaylandPlatform* platform = data;
    const mwinXkbApi* xkb = &platform->xkb;
    mwinWaylandKeyboard* keyboard = &platform->keyboard;
    if (format != WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1 || keyboard->xkb.context == nullptr || size == 0)
    {
        close(fd);
        return;
    }
    void* text = mmap(nullptr, size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    if (text == MAP_FAILED)
    {
        return;
    }
    // The text ends in a NUL, which the size counts.
    struct xkb_keymap* keymap =
        xkb->keymapNewFromBuffer(keyboard->xkb.context, text, strnlen(text, size),
                                 XKB_KEYMAP_FORMAT_TEXT_V1, XKB_KEYMAP_COMPILE_NO_FLAGS);
    munmap(text, size);
    struct xkb_state* state = keymap != nullptr ? xkb->stateNew(keymap) : nullptr;
    if (state == nullptr)
    {
        if (keymap != nullptr)
        {
            xkb->keymapUnref(keymap);
        }
        return;
    }
    mwinXkbSetKeymap(&keyboard->xkb, keymap, state);
    keyboard->repeatKey = 0;
    PostLayoutChange(platform);
}

static void OnEnter(void* data, struct wl_keyboard* object, uint32_t serial,
                    struct wl_surface* surface, struct wl_array* keys)
{
    (void)object;
    (void)keys;
    mwinWaylandPlatform* platform = data;
    platform->inputSerial = serial;
    // Keys already held when focus comes are not reported as pressed.
    platform->keyboard.focus = mwinWaylandSlotOf(platform, surface);
    platform->keyboard.held = 0;
    platform->keyboard.repeatKey = 0;
}

static void OnLeave(void* data, struct wl_keyboard* object, uint32_t serial,
                    struct wl_surface* surface)
{
    (void)object;
    (void)serial;
    (void)surface;
    mwinWaylandPlatform* platform = data;
    mwinWaylandKeyboard* keyboard = &platform->keyboard;
    if (keyboard->focus >= 0 && keyboard->held > 0)
    {
        // Their releases will not come.
        mwinEvent reset = {0};
        reset.type = mwin_eventInputStateReset;
        reset.timeNs = mwinMonotonicNow();
        mwinPost(platform->context, (uint32_t)keyboard->focus, &reset);
    }
    keyboard->focus = -1;
    keyboard->held = 0;
    keyboard->repeatKey = 0;
    mwinXkbResetCompose(&keyboard->xkb);
}

// Posts a key and, for one that goes down, the text it types.
static void PostKey(mwinWaylandPlatform* platform, mwinEventType type, uint32_t evdev, bool repeat,
                    uint64_t timeNs)
{
    mwinWaylandKeyboard* keyboard = &platform->keyboard;
    mwinKeyCode code = mwinKeyCodeFromEvdev(evdev);
    mwinEvent event = {0};
    event.type = type;
    event.timeNs = timeNs;
    event.data.key = (mwinKeyEvent){code, keyboard->xkb.modifiers,
                                    mwinXkbKeyOf(&keyboard->xkb, evdev, code), repeat};
    mwinPost(platform->context, (uint32_t)keyboard->focus, &event);
    char text[MWIN_XKB_TEXT_BYTES];
    uint32_t length =
        type == mwin_eventKeyDown ? mwinXkbType(&keyboard->xkb, evdev, !repeat, text) : 0;
    if (length > 0)
    {
        mwinEvent typed = {0};
        typed.type = mwin_eventTextInput;
        typed.timeNs = timeNs;
        typed.data.text = (mwinTextEvent){text, length};
        mwinPost(platform->context, (uint32_t)keyboard->focus, &typed);
    }
}

static void OnKey(void* data, struct wl_keyboard* object, uint32_t serial, uint32_t time,
                  uint32_t evdev, uint32_t state)
{
    (void)object;
    mwinWaylandPlatform* platform = data;
    platform->inputSerial = serial;
    mwinWaylandKeyboard* keyboard = &platform->keyboard;
    if (keyboard->focus < 0 || keyboard->xkb.state == nullptr)
    {
        return;
    }
    uint64_t timeNs = mwinMonotonicFromMilliseconds(time);
    if (state != WL_KEYBOARD_KEY_STATE_PRESSED)
    {
        keyboard->held -= keyboard->held > 0 ? 1 : 0;
        keyboard->repeatKey = evdev == keyboard->repeatKey ? 0 : keyboard->repeatKey;
        PostKey(platform, mwin_eventKeyUp, evdev, false, timeNs);
        return;
    }
    keyboard->held += 1;
    PostKey(platform, mwin_eventKeyDown, evdev, false, timeNs);
    if (keyboard->repeatRate > 0 && mwinXkbRepeats(&keyboard->xkb, evdev))
    {
        keyboard->repeatKey = evdev;
        keyboard->repeatNextNs = timeNs + (uint64_t)keyboard->repeatDelayMs * 1000000u;
    }
}

static void OnModifiers(void* data, struct wl_keyboard* object, uint32_t serial, uint32_t depressed,
                        uint32_t latched, uint32_t locked, uint32_t group)
{
    (void)object;
    (void)serial;
    mwinWaylandPlatform* platform = data;
    if (mwinXkbUpdateState(&platform->keyboard.xkb, depressed, latched, locked, 0, 0, group))
    {
        PostLayoutChange(platform);
    }
}

static void OnRepeatInfo(void* data, struct wl_keyboard* object, int32_t rate, int32_t delay)
{
    (void)object;
    mwinWaylandKeyboard* keyboard = &((mwinWaylandPlatform*)data)->keyboard;
    keyboard->repeatRate = rate > 0 ? rate : 0;
    keyboard->repeatDelayMs = delay > 0 ? delay : 0;
}

static const struct wl_keyboard_listener s_keyboardListener = {
    OnKeymap, OnEnter, OnLeave, OnKey, OnModifiers, OnRepeatInfo,
};

void mwinWaylandAddKeyboard(mwinWaylandPlatform* platform)
{
    mwinWaylandKeyboard* keyboard = &platform->keyboard;
    *keyboard = (mwinWaylandKeyboard){.focus = -1, .repeatRate = 25, .repeatDelayMs = 600};
    if (platform->xkb.library == nullptr || !mwinXkbStart(&keyboard->xkb, &platform->xkb))
    {
        return;
    }
    keyboard->keyboard = mwinWlRequest(&platform->api, platform->seat, WL_SEAT_GET_KEYBOARD,
                                       &wl_keyboard_interface, 0);
    mwinWlListen(&platform->api, keyboard->keyboard, &s_keyboardListener, platform);
}

void mwinWaylandRemoveKeyboard(mwinWaylandPlatform* platform)
{
    const mwinWaylandApi* api = &platform->api;
    mwinWaylandKeyboard* keyboard = &platform->keyboard;
    if (keyboard->keyboard != nullptr)
    {
        if (mwinWlVersion(api, keyboard->keyboard) >= WL_KEYBOARD_RELEASE_SINCE_VERSION)
        {
            (void)mwinWlRequest(api, keyboard->keyboard, WL_KEYBOARD_RELEASE, nullptr,
                                WL_MARSHAL_FLAG_DESTROY);
        }
        else
        {
            api->proxyDestroy((struct wl_proxy*)keyboard->keyboard);
        }
    }
    mwinXkbStop(&keyboard->xkb);
    *keyboard = (mwinWaylandKeyboard){.focus = -1};
}

void mwinWaylandRepeatKeys(mwinWaylandPlatform* platform)
{
    mwinWaylandKeyboard* keyboard = &platform->keyboard;
    if (keyboard->repeatKey == 0 || keyboard->focus < 0 || keyboard->repeatRate <= 0)
    {
        return;
    }
    uint64_t now = mwinMonotonicNow();
    uint64_t interval = 1000000000u / (uint64_t)keyboard->repeatRate;
    for (int i = 0; i < MAX_REPEATS_PER_PUMP && keyboard->repeatNextNs <= now; i++)
    {
        PostKey(platform, mwin_eventKeyDown, keyboard->repeatKey, true, keyboard->repeatNextNs);
        keyboard->repeatNextNs += interval;
    }
    if (keyboard->repeatNextNs <= now)
    {
        keyboard->repeatNextNs = now + interval;
    }
}

void mwinWaylandForgetKeyboardFocus(mwinWaylandPlatform* platform, uint32_t slot)
{
    if (platform->keyboard.focus == (int32_t)slot)
    {
        platform->keyboard.focus = -1;
        platform->keyboard.repeatKey = 0;
    }
}

mwinKey mwinWaylandMapKeyCode(const mwinWaylandPlatform* platform, mwinKeyCode code)
{
    return mwinXkbMapKeyCode(&platform->keyboard.xkb, code);
}

mwinResult mwinWaylandKeyboardLayout(const mwinWaylandPlatform* platform, char* buffer,
                                     size_t capacity, size_t* lengthOut)
{
    return mwinXkbLayoutName(&platform->keyboard.xkb, buffer, capacity, lengthOut);
}
