// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// X11 keyboard and pointer input.

#include "x11_input.h"

#include "allocator.h"
#include "evdev.h"
#include "x11_chrome.h"
#include "xkb_keyboard.h"

// X11 key codes are evdev codes plus 8.
#define XKB_OFFSET 8

// Reads the keymap and state of the keyboard device anew.
static bool ReadKeymap(mwinX11Platform* platform)
{
    const mwinX11Api* api = &platform->api;
    mwinX11Keyboard* keyboard = &platform->keyboard;
    struct xkb_keymap* keymap = api->xkbKeymapFromDevice(
        keyboard->xkb.context, platform->connection, keyboard->device, XKB_KEYMAP_COMPILE_NO_FLAGS);
    struct xkb_state* state =
        keymap != nullptr ? api->xkbStateFromDevice(keymap, platform->connection, keyboard->device)
                          : nullptr;
    if (state == nullptr)
    {
        if (keymap != nullptr)
        {
            platform->xkbApi.keymapUnref(keymap);
        }
        return false;
    }
    mwinXkbSetKeymap(&keyboard->xkb, keymap, state);
    return true;
}

// Asks for the XKB events that change the keymap and the state, and for
// repeats without releases between them.
static void SelectEvents(const mwinX11Platform* platform)
{
    const mwinX11Api* api = &platform->api;
    const uint16_t events = XCB_XKB_EVENT_TYPE_NEW_KEYBOARD_NOTIFY | XCB_XKB_EVENT_TYPE_MAP_NOTIFY |
                            XCB_XKB_EVENT_TYPE_STATE_NOTIFY;
    const uint16_t mapParts = XCB_XKB_MAP_PART_KEY_TYPES | XCB_XKB_MAP_PART_KEY_SYMS |
                              XCB_XKB_MAP_PART_MODIFIER_MAP | XCB_XKB_MAP_PART_EXPLICIT_COMPONENTS |
                              XCB_XKB_MAP_PART_KEY_ACTIONS | XCB_XKB_MAP_PART_VIRTUAL_MODS |
                              XCB_XKB_MAP_PART_VIRTUAL_MOD_MAP;
    const uint16_t stateParts = XCB_XKB_STATE_PART_MODIFIER_BASE |
                                XCB_XKB_STATE_PART_MODIFIER_LATCH |
                                XCB_XKB_STATE_PART_MODIFIER_LOCK | XCB_XKB_STATE_PART_GROUP_BASE |
                                XCB_XKB_STATE_PART_GROUP_LATCH | XCB_XKB_STATE_PART_GROUP_LOCK;
    xcb_xkb_select_events_details_t details = {0};
    details.affectNewKeyboard = XCB_XKB_NKN_DETAIL_KEYCODES;
    details.newKeyboardDetails = XCB_XKB_NKN_DETAIL_KEYCODES;
    details.affectState = stateParts;
    details.stateDetails = stateParts;
    api->xkbSelectEvents(platform->connection, (xcb_xkb_device_spec_t)platform->keyboard.device,
                         events, 0, 0, mapParts, mapParts, &details);
    const uint32_t repeat = XCB_XKB_PER_CLIENT_FLAG_DETECTABLE_AUTO_REPEAT;
    mwinReleaseSystemMemory(api->xkbPerClientFlagsReply(
        platform->connection,
        api->xkbPerClientFlags(platform->connection, XCB_XKB_ID_USE_CORE_KBD, repeat, repeat, 0, 0,
                               0),
        nullptr));
}

void mwinX11StartKeyboard(mwinX11Platform* platform)
{
    const mwinX11Api* api = &platform->api;
    mwinX11Keyboard* keyboard = &platform->keyboard;
    if (api->xkbX11Library == nullptr || mwinLoadXkb(&platform->xkbApi) != mwin_success)
    {
        return;
    }
    uint8_t event = 0;
    if (api->xkbSetupExtension(platform->connection, XKB_X11_MIN_MAJOR_XKB_VERSION,
                               XKB_X11_MIN_MINOR_XKB_VERSION, XKB_X11_SETUP_XKB_EXTENSION_NO_FLAGS,
                               nullptr, nullptr, &event, nullptr) == 0 ||
        !mwinXkbStart(&keyboard->xkb, &platform->xkbApi))
    {
        return;
    }
    keyboard->device = api->xkbCoreDevice(platform->connection);
    if (keyboard->device < 0 || !ReadKeymap(platform))
    {
        mwinXkbStop(&keyboard->xkb);
        return;
    }
    keyboard->event = event;
    SelectEvents(platform);
}

void mwinX11StopKeyboard(mwinX11Platform* platform)
{
    mwinXkbStop(&platform->keyboard.xkb);
    mwinUnloadXkb(&platform->xkbApi);
}

static void PostLayoutChange(mwinX11Platform* platform)
{
    mwinEvent event = {0};
    event.type = mwin_eventKeyboardLayoutChanged;
    event.timeNs = mwinMonotonicNow();
    mwinPostGlobal(platform->context, &event);
}

// An XKB event: the keymap or the state changed.
static void OnXkb(mwinX11Platform* platform, const xcb_generic_event_t* event)
{
    mwinX11Keyboard* keyboard = &platform->keyboard;
    // Every XKB event shares one event code and has its kind next.
    uint8_t kind = ((const uint8_t*)event)[1];
    if (kind == XCB_XKB_NEW_KEYBOARD_NOTIFY || kind == XCB_XKB_MAP_NOTIFY)
    {
        if (ReadKeymap(platform))
        {
            PostLayoutChange(platform);
        }
        return;
    }
    if (kind != XCB_XKB_STATE_NOTIFY)
    {
        return;
    }
    const xcb_xkb_state_notify_event_t* state = (const xcb_xkb_state_notify_event_t*)event;
    if (mwinXkbUpdateState(&keyboard->xkb, state->baseMods, state->latchedMods, state->lockedMods,
                           (uint32_t)state->baseGroup, (uint32_t)state->latchedGroup,
                           state->lockedGroup))
    {
        PostLayoutChange(platform);
    }
}

static void OnKey(mwinX11Platform* platform, const xcb_key_press_event_t* event, bool pressed)
{
    mwinX11Keyboard* keyboard = &platform->keyboard;
    int32_t slot = mwinX11SlotOf(platform, event->event);
    if (slot < 0 || keyboard->xkb.state == nullptr || event->detail < XKB_OFFSET)
    {
        return;
    }
    uint32_t evdev = event->detail - XKB_OFFSET;
    uint8_t bit = (uint8_t)(1u << (event->detail & 7u));
    bool repeat = pressed && (keyboard->held[event->detail >> 3] & bit) != 0;
    keyboard->held[event->detail >> 3] = pressed
                                             ? keyboard->held[event->detail >> 3] | bit
                                             : keyboard->held[event->detail >> 3] & (uint8_t)~bit;
    mwinKeyCode code = mwinKeyCodeFromEvdev(evdev);
    mwinEvent record = {0};
    record.type = pressed ? mwin_eventKeyDown : mwin_eventKeyUp;
    record.timeNs = mwinMonotonicFromMilliseconds(event->time);
    record.data.key = (mwinKeyEvent){code, keyboard->xkb.modifiers,
                                     mwinXkbKeyOf(&keyboard->xkb, evdev, code), repeat};
    char text[MWIN_XKB_TEXT_BYTES];
    uint32_t length = pressed ? mwinXkbType(&keyboard->xkb, evdev, !repeat, text) : 0;
    // An input method may take the key: it answers later (mwin-0030).
    uint32_t keysym = platform->xkbApi.stateKeyGetOneSym(keyboard->xkb.state, event->detail);
    if (mwinImeOffer(&platform->ime, (uint32_t)slot, keysym, event->detail, event->state, &record,
                     text, length, mwinMonotonicNow()))
    {
        return;
    }
    mwinPost(platform->context, (uint32_t)slot, &record);
    if (length > 0)
    {
        mwinEvent typed = {0};
        typed.type = mwin_eventTextInput;
        typed.timeNs = record.timeNs;
        typed.data.text = (mwinTextEvent){text, length};
        mwinPost(platform->context, (uint32_t)slot, &typed);
    }
}

static void PostPointer(mwinX11Platform* platform, uint32_t slot, mwinEventType type,
                        mwinMouseButton button, uint64_t timeNs)
{
    const mwinX11Pointer* pointer = &platform->pointer;
    mwinEvent event = {0};
    event.type = type;
    event.timeNs = timeNs;
    event.data.pointer = (mwinPointerEvent){pointer->position, platform->keyboard.xkb.modifiers,
                                            pointer->buttons, button, pointer->clicks.clicks};
    mwinPost(platform->context, slot, &event);
}

static mwinPosition PositionOf(const mwinX11Platform* platform, int16_t x, int16_t y)
{
    return (mwinPosition){(float)x / platform->scale, (float)y / platform->scale};
}

static void PostWheel(mwinX11Platform* platform, uint32_t slot, float x, float y, uint64_t timeNs)
{
    mwinEvent event = {0};
    event.type = mwin_eventWheel;
    event.timeNs = timeNs;
    event.data.wheel = (mwinWheelEvent){x, y};
    mwinPost(platform->context, slot, &event);
}

// The wheel turned: X11's buttons 4 to 7, a detent each.
static void Turn(mwinX11Platform* platform, uint32_t slot, uint8_t button, uint64_t timeNs)
{
    PostWheel(platform, slot, button == 6 ? -1.0f : (button == 7 ? 1.0f : 0.0f),
              button == 4 ? 1.0f : (button == 5 ? -1.0f : 0.0f), timeNs);
}

static void OnButton(mwinX11Platform* platform, const xcb_button_press_event_t* event, bool pressed)
{
    static const mwinMouseButton buttons[10] = {
        0, mwin_buttonLeft, mwin_buttonMiddle, mwin_buttonRight, 0, 0, 0,
        0, mwin_buttonBack, mwin_buttonForward};
    mwinX11Pointer* pointer = &platform->pointer;
    int32_t slot = mwinX11SlotOf(platform, event->event);
    if (slot < 0 || event->detail >= 10)
    {
        return;
    }
    uint64_t timeNs = mwinMonotonicFromMilliseconds(event->time);
    pointer->position = PositionOf(platform, event->event_x, event->event_y);
    if (event->detail >= 4 && event->detail <= 7)
    {
        if (pressed)
        {
            Turn(platform, (uint32_t)slot, event->detail, timeNs);
        }
        return;
    }
    mwinMouseButton button = buttons[event->detail];
    uint8_t bit = (uint8_t)(1u << (button - 1));
    if (pressed)
    {
        uint8_t clicks = mwinCountClick(&pointer->clicks, button, pointer->position, timeNs);
        if (button == mwin_buttonLeft &&
            mwinX11PressChrome(platform, (uint32_t)slot, event, pointer->position, clicks))
        {
            return;
        }
        pointer->buttons |= bit;
    }
    else if ((pointer->buttons & bit) == 0)
    {
        // The release of a press the window manager took.
        return;
    }
    else
    {
        pointer->buttons &= (uint8_t)~bit;
    }
    PostPointer(platform, (uint32_t)slot, pressed ? mwin_eventButtonDown : mwin_eventButtonUp,
                button, timeNs);
}

static void OnMotion(mwinX11Platform* platform, const xcb_motion_notify_event_t* event)
{
    int32_t slot = mwinX11SlotOf(platform, event->event);
    if (slot >= 0)
    {
        platform->pointer.position = PositionOf(platform, event->event_x, event->event_y);
        PostPointer(platform, (uint32_t)slot, mwin_eventCursorMoved, 0,
                    mwinMonotonicFromMilliseconds(event->time));
    }
}

// The pointer crossed into or out of a window; crossings a grab makes
// are not the pointer's.
static void OnCrossing(mwinX11Platform* platform, const xcb_enter_notify_event_t* event,
                       bool entered)
{
    mwinX11Pointer* pointer = &platform->pointer;
    int32_t slot = mwinX11SlotOf(platform, event->event);
    if (slot < 0 || event->mode != XCB_NOTIFY_MODE_NORMAL)
    {
        return;
    }
    uint64_t timeNs = mwinMonotonicFromMilliseconds(event->time);
    pointer->position = PositionOf(platform, event->event_x, event->event_y);
    if (entered)
    {
        // The scroll valuators counted on while the pointer was away.
        mwinX11RestartScroll(&platform->scroll);
        pointer->focus = slot;
        PostPointer(platform, (uint32_t)slot, mwin_eventCursorEntered, 0, timeNs);
        return;
    }
    PostPointer(platform, (uint32_t)slot, mwin_eventCursorLeft, 0, timeNs);
    pointer->focus = -1;
}

// The X input type of a device (XListInputDevices), XCB_ATOM_NONE where
// it has none.
static xcb_atom_t TypeOf(const mwinX11Platform* platform,
                         const xcb_input_list_input_devices_reply_t* list, uint16_t device)
{
    const mwinX11Api* api = &platform->api;
    const xcb_input_device_info_t* devices = list != nullptr ? api->inputDevices(list) : nullptr;
    int count = list != nullptr ? api->inputDevicesLength(list) : 0;
    for (int i = 0; i < count; i++)
    {
        if (devices[i].device_id == device)
        {
            return devices[i].device_type;
        }
    }
    return XCB_ATOM_NONE;
}

// The pen axis a valuator's label names, or mwin_x11PenAxes for none.
static mwinX11PenAxis PenAxisOf(const mwinX11Platform* platform, xcb_atom_t label)
{
    const xcb_atom_t* atoms = platform->atoms;
    return label == XCB_ATOM_NONE                 ? mwin_x11PenAxes
           : label == atoms[mwin_atomAbsPressure] ? mwin_x11PenPressure
           : label == atoms[mwin_atomAbsTiltX]    ? mwin_x11PenTiltX
           : label == atoms[mwin_atomAbsTiltY]    ? mwin_x11PenTiltY
                                                  : mwin_x11PenAxes;
}

// A slave pointer device: its scroll valuators, whether it is a pen, with
// the valuators of its pressure and tilt, and whether it is a touch
// screen, with the valuator of its touches' pressure.
static void ReadDevice(mwinX11Platform* platform, const xcb_input_xi_device_info_t* info,
                       xcb_atom_t type)
{
    const mwinX11Api* api = &platform->api;
    bool pressure = false;
    bool direct = false;
    for (xcb_input_device_class_iterator_t item = api->deviceClasses(info); item.rem > 0;
         api->deviceClassNext(&item))
    {
        const xcb_input_scroll_class_t* scroll = (const xcb_input_scroll_class_t*)item.data;
        const xcb_input_valuator_class_t* valuator = (const xcb_input_valuator_class_t*)item.data;
        const xcb_input_touch_class_t* touch = (const xcb_input_touch_class_t*)item.data;
        direct = direct || (item.data->type == XCB_INPUT_DEVICE_CLASS_TYPE_TOUCH &&
                            touch->mode == XCB_INPUT_TOUCH_MODE_DIRECT);
        if (item.data->type == XCB_INPUT_DEVICE_CLASS_TYPE_SCROLL)
        {
            mwinX11AddScrollAxis(&platform->scroll, info->deviceid, scroll->number,
                                 scroll->scroll_type == XCB_INPUT_SCROLL_TYPE_HORIZONTAL,
                                 scroll->increment);
        }
        pressure = pressure || (item.data->type == XCB_INPUT_DEVICE_CLASS_TYPE_VALUATOR &&
                                PenAxisOf(platform, valuator->label) == mwin_x11PenPressure);
    }
    const mwinX11PenTypes types = {platform->atoms[mwin_atomStylus],
                                   platform->atoms[mwin_atomEraser],
                                   platform->atoms[mwin_atomTablet]};
    mwinX11PenKind kind = mwinX11PenKindOf(type, types, api->deviceName(info),
                                           (size_t)api->deviceNameLength(info), pressure);
    mwinX11Pen* pen = kind != mwin_x11NotPen
                          ? mwinX11AddPen(&platform->pens, info->deviceid, kind == mwin_x11Eraser)
                          : nullptr;
    for (xcb_input_device_class_iterator_t item = api->deviceClasses(info);
         pen != nullptr && item.rem > 0; api->deviceClassNext(&item))
    {
        const xcb_input_valuator_class_t* valuator = (const xcb_input_valuator_class_t*)item.data;
        mwinX11PenAxis axis = item.data->type == XCB_INPUT_DEVICE_CLASS_TYPE_VALUATOR
                                  ? PenAxisOf(platform, valuator->label)
                                  : mwin_x11PenAxes;
        if (axis != mwin_x11PenAxes)
        {
            mwinX11SetPenAxis(pen, axis, valuator->number, valuator->min, valuator->max);
        }
    }
    mwinX11TouchDevice* screen =
        direct ? mwinX11AddTouchDevice(&platform->touches, info->deviceid) : nullptr;
    for (xcb_input_device_class_iterator_t item = api->deviceClasses(info);
         screen != nullptr && item.rem > 0; api->deviceClassNext(&item))
    {
        const xcb_input_valuator_class_t* valuator = (const xcb_input_valuator_class_t*)item.data;
        if (item.data->type == XCB_INPUT_DEVICE_CLASS_TYPE_VALUATOR &&
            valuator->label != XCB_ATOM_NONE &&
            valuator->label == platform->atoms[mwin_atomAbsMtPressure])
        {
            mwinX11SetTouchPressure(screen, valuator->number, valuator->min, valuator->max);
        }
    }
}

// Every pointer device read anew: the scroll valuators, and the pens.
static void ReadDevices(mwinX11Platform* platform)
{
    const mwinX11Api* api = &platform->api;
    platform->scroll = (mwinX11Scroll){0};
    platform->pens = (mwinX11Pens){0};
    mwinX11ClearTouchDevices(&platform->touches);
    xcb_input_list_input_devices_reply_t* list = api->listInputDevicesReply(
        platform->connection, api->listInputDevices(platform->connection), nullptr);
    xcb_input_xi_query_device_reply_t* reply = api->xiQueryDeviceReply(
        platform->connection, api->xiQueryDevice(platform->connection, XCB_INPUT_DEVICE_ALL),
        nullptr);
    for (xcb_input_xi_device_info_iterator_t info = api->deviceInfos(reply);
         reply != nullptr && info.rem > 0; api->deviceInfoNext(&info))
    {
        if (info.data->type == XCB_INPUT_DEVICE_TYPE_SLAVE_POINTER)
        {
            ReadDevice(platform, info.data, TypeOf(platform, list, info.data->deviceid));
        }
    }
    mwinReleaseSystemMemory(reply);
    mwinReleaseSystemMemory(list);
}

void mwinX11StartRawMotion(mwinX11Platform* platform)
{
    const mwinX11Api* api = &platform->api;
    if (api->xinputLibrary == nullptr)
    {
        return;
    }
    const xcb_query_extension_reply_t* extension =
        api->getExtensionData(platform->connection, api->xinputId);
    if (extension == nullptr || !extension->present)
    {
        return;
    }
    xcb_input_xi_query_version_reply_t* version = api->xiQueryVersionReply(
        platform->connection, api->xiQueryVersion(platform->connection, 2, 2), nullptr);
    bool recent = version != nullptr && version->major_version >= 2;
    platform->smoothScroll = recent && (version->major_version > 2 || version->minor_version >= 1);
    platform->touch = recent && (version->major_version > 2 || version->minor_version >= 2);
    mwinReleaseSystemMemory(version);
    if (!recent)
    {
        return;
    }
    // Raw motion from the master devices, and the changes of every
    // device.
    const struct
    {
        xcb_input_event_mask_t head;
        uint32_t mask;
    } select[2] = {
        {{XCB_INPUT_DEVICE_ALL_MASTER, 1}, XCB_INPUT_XI_EVENT_MASK_RAW_MOTION},
        {{XCB_INPUT_DEVICE_ALL, 1},
         XCB_INPUT_XI_EVENT_MASK_DEVICE_CHANGED | XCB_INPUT_XI_EVENT_MASK_HIERARCHY},
    };
    api->xiSelectEvents(platform->connection, platform->screen->root,
                        platform->smoothScroll ? 2 : 1, &select[0].head);
    platform->xinputOpcode = extension->major_opcode;
    if (platform->smoothScroll)
    {
        ReadDevices(platform);
    }
}

void mwinX11SelectPointer(const mwinX11Platform* platform, xcb_window_t window)
{
    if (!platform->smoothScroll)
    {
        return;
    }
    const struct
    {
        xcb_input_event_mask_t head;
        uint32_t mask;
    } select = {{XCB_INPUT_DEVICE_ALL_MASTER, 1},
                XCB_INPUT_XI_EVENT_MASK_BUTTON_PRESS | XCB_INPUT_XI_EVENT_MASK_BUTTON_RELEASE |
                    XCB_INPUT_XI_EVENT_MASK_MOTION |
                    // The three together or none; selecting them stops the
                    // pointer the server makes from a touch.
                    (platform->touch ? XCB_INPUT_XI_EVENT_MASK_TOUCH_BEGIN |
                                           XCB_INPUT_XI_EVENT_MASK_TOUCH_UPDATE |
                                           XCB_INPUT_XI_EVENT_MASK_TOUCH_END
                                     : 0u)};
    platform->api.xiSelectEvents(platform->connection, window, 1, &select.head);
}

// The window whose cursor is captured and that has focus, or -1.
static int32_t CapturingWindow(const mwinX11Platform* platform)
{
    for (uint32_t i = 0; i < platform->context->limits.windows; i++)
    {
        if (platform->windows[i].window != 0 &&
            platform->windows[i].cursorMode == mwin_cursorCaptured &&
            platform->context->windows[i].state.focused)
        {
            return (int32_t)i;
        }
    }
    return -1;
}

static float FixedValue(xcb_input_fp3232_t value)
{
    return (float)((double)value.integral + (double)value.frac / 4294967296.0);
}

// Raw motion: the first two valuators, before acceleration.
static void OnRawMotion(mwinX11Platform* platform, const xcb_input_raw_motion_event_t* event)
{
    const mwinX11Api* api = &platform->api;
    int32_t slot = CapturingWindow(platform);
    if (slot < 0)
    {
        return;
    }
    const uint32_t* mask = api->rawValuatorMask(event);
    int words = api->rawValuatorMaskLength(event);
    const xcb_input_fp3232_t* values = api->rawAxisValues(event);
    int count = api->rawAxisValuesLength(event);
    float delta[2] = {0.0f, 0.0f};
    int index = 0;
    for (int bit = 0; bit < words * 32 && index < count; bit++)
    {
        if ((mask[bit / 32] & (1u << (bit % 32))) == 0)
        {
            continue;
        }
        if (bit < 2)
        {
            delta[bit] = FixedValue(values[index]);
        }
        index += 1;
    }
    mwinEvent record = {0};
    record.type = mwin_eventRawPointerDelta;
    record.timeNs = mwinMonotonicFromMilliseconds(event->time);
    record.data.delta = (mwinDeltaEvent){delta[0], delta[1]};
    mwinPost(platform->context, (uint32_t)slot, &record);
}

// An XI2 pointer event's fixed point place as the core one's pixel.
static int16_t Pixel(xcb_input_fp1616_t value)
{
    return (int16_t)(value >> 16);
}

// An XI2 place, fixed point in pixels, in logical units.
static mwinPosition PlaceOf(const mwinX11Platform* platform, xcb_input_fp1616_t x,
                            xcb_input_fp1616_t y)
{
    return (mwinPosition){(float)x / 65536.0f / platform->scale,
                          (float)y / 65536.0f / platform->scale};
}

// A pen's XI2 event as its pen record (mwin-0037), in place of a mouse
// record.
static void PostPen(mwinX11Platform* platform, mwinX11Pen* pen, mwinX11PenInput input,
                    const xcb_input_button_press_event_t* event)
{
    const mwinX11Api* api = &platform->api;
    int32_t slot = mwinX11SlotOf(platform, event->event);
    mwinEvent record;
    if (mwinX11PenRecordOf(pen, input, event->detail,
                           PlaceOf(platform, event->event_x, event->event_y),
                           api->valuatorMask(event), api->valuatorMaskLength(event),
                           api->axisValues(event), api->axisValuesLength(event), &record) > 0 &&
        slot >= 0)
    {
        record.timeNs = mwinMonotonicFromMilliseconds(event->time);
        mwinPost(platform->context, (uint32_t)slot, &record);
    }
}

// An XI2 motion: a pen's, or the wheel's movement where it carries
// scroll valuators, and the pointer's where it moved.
static void OnXiMotion(mwinX11Platform* platform, const xcb_input_motion_event_t* event)
{
    const mwinX11Api* api = &platform->api;
    mwinX11Pen* pen = mwinX11FindPen(&platform->pens, event->sourceid);
    if (pen != nullptr)
    {
        PostPen(platform, pen, mwin_x11PenMotion, event);
        return;
    }
    int32_t slot = mwinX11SlotOf(platform, event->event);
    float x = 0.0f;
    float y = 0.0f;
    bool scrolled = mwinX11Scrolled(&platform->scroll, event->sourceid, api->valuatorMask(event),
                                    api->valuatorMaskLength(event), api->axisValues(event),
                                    api->axisValuesLength(event), &x, &y);
    if (slot >= 0 && (x != 0.0f || y != 0.0f))
    {
        PostWheel(platform, (uint32_t)slot, x, y, mwinMonotonicFromMilliseconds(event->time));
    }
    mwinPosition position = PositionOf(platform, Pixel(event->event_x), Pixel(event->event_y));
    if (scrolled && position.x == platform->pointer.position.x &&
        position.y == platform->pointer.position.y)
    {
        return;
    }
    const xcb_motion_notify_event_t core = {
        .response_type = XCB_MOTION_NOTIFY,
        .time = event->time,
        .root = event->root,
        .event = event->event,
        .child = event->child,
        .root_x = Pixel(event->root_x),
        .root_y = Pixel(event->root_y),
        .event_x = Pixel(event->event_x),
        .event_y = Pixel(event->event_y),
        .state = (uint16_t)event->mods.effective,
        .same_screen = 1,
    };
    OnMotion(platform, &core);
}

// An XI2 press or release: a pen's, or read as the core one; the wheel's
// buttons the server made from scroll valuators are dropped.
static void OnXiButton(mwinX11Platform* platform, const xcb_input_button_press_event_t* event,
                       bool pressed)
{
    if ((event->flags & XCB_INPUT_POINTER_EVENT_FLAGS_POINTER_EMULATED) != 0 ||
        event->detail > UINT8_MAX)
    {
        return;
    }
    mwinX11Pen* pen = mwinX11FindPen(&platform->pens, event->sourceid);
    if (pen != nullptr)
    {
        PostPen(platform, pen, pressed ? mwin_x11PenPress : mwin_x11PenRelease, event);
        return;
    }
    const xcb_button_press_event_t core = {
        .response_type = pressed ? XCB_BUTTON_PRESS : XCB_BUTTON_RELEASE,
        .detail = (uint8_t)event->detail,
        .time = event->time,
        .root = event->root,
        .event = event->event,
        .child = event->child,
        .root_x = Pixel(event->root_x),
        .root_y = Pixel(event->root_y),
        .event_x = Pixel(event->event_x),
        .event_y = Pixel(event->event_y),
        .state = (uint16_t)event->mods.effective,
        .same_screen = 1,
    };
    platform->pointer.device = event->deviceid;
    OnButton(platform, &core, pressed);
    platform->pointer.device = 0;
}

// A touch screen's touch event as its touch record (mwin-0039). It has
// the layout of a button event, whose valuator readers it takes.
static void OnXiTouch(mwinX11Platform* platform, const xcb_input_touch_begin_event_t* event,
                      mwinX11TouchInput input)
{
    const mwinX11Api* api = &platform->api;
    const xcb_input_button_press_event_t* fields = (const xcb_input_button_press_event_t*)event;
    int32_t slot = mwinX11SlotOf(platform, event->event);
    mwinEvent record;
    if (mwinX11TouchRecordOf(&platform->touches, event->sourceid, input, event->detail,
                             PlaceOf(platform, event->event_x, event->event_y),
                             api->valuatorMask(fields), api->valuatorMaskLength(fields),
                             api->axisValues(fields), api->axisValuesLength(fields), &record) > 0 &&
        slot >= 0)
    {
        record.timeNs = mwinMonotonicFromMilliseconds(event->time);
        mwinPost(platform->context, (uint32_t)slot, &record);
    }
}

// An XInput event: raw motion, the pointer and touches through XI2, or a
// change of the devices.
static void OnXi(mwinX11Platform* platform, const xcb_ge_generic_event_t* event)
{
    switch (event->event_type)
    {
    case XCB_INPUT_RAW_MOTION:
        OnRawMotion(platform, (const xcb_input_raw_motion_event_t*)event);
        break;
    case XCB_INPUT_MOTION:
        OnXiMotion(platform, (const xcb_input_motion_event_t*)event);
        break;
    case XCB_INPUT_BUTTON_PRESS:
    case XCB_INPUT_BUTTON_RELEASE:
        OnXiButton(platform, (const xcb_input_button_press_event_t*)event,
                   event->event_type == XCB_INPUT_BUTTON_PRESS);
        break;
    case XCB_INPUT_TOUCH_BEGIN:
        OnXiTouch(platform, (const xcb_input_touch_begin_event_t*)event, mwin_x11TouchBegin);
        break;
    case XCB_INPUT_TOUCH_UPDATE:
        OnXiTouch(platform, (const xcb_input_touch_begin_event_t*)event, mwin_x11TouchUpdate);
        break;
    case XCB_INPUT_TOUCH_END:
        OnXiTouch(platform, (const xcb_input_touch_begin_event_t*)event, mwin_x11TouchEnd);
        break;
    case XCB_INPUT_DEVICE_CHANGED:
        // A master switched devices, whose valuators count from anew, or
        // a device's classes changed.
        if (((const xcb_input_device_changed_event_t*)event)->reason ==
            XCB_INPUT_CHANGE_REASON_SLAVE_SWITCH)
        {
            mwinX11RestartScroll(&platform->scroll);
            break;
        }
        ReadDevices(platform);
        break;
    case XCB_INPUT_HIERARCHY:
        ReadDevices(platform);
        break;
    default:
        break;
    }
}

bool mwinX11HandleInputEvent(mwinX11Platform* platform, const xcb_generic_event_t* event)
{
    uint8_t type = event->response_type & 0x7F;
    if (platform->keyboard.event != 0 && type == platform->keyboard.event)
    {
        OnXkb(platform, event);
        return true;
    }
    if (type == XCB_GE_GENERIC && platform->xinputOpcode != 0)
    {
        const xcb_ge_generic_event_t* generic = (const xcb_ge_generic_event_t*)event;
        if (generic->extension == platform->xinputOpcode)
        {
            OnXi(platform, generic);
        }
        return true;
    }
    switch (type)
    {
    case XCB_KEY_PRESS:
    case XCB_KEY_RELEASE:
        platform->inputTime = ((const xcb_key_press_event_t*)event)->time;
        OnKey(platform, (const xcb_key_press_event_t*)event, type == XCB_KEY_PRESS);
        return true;
    case XCB_BUTTON_PRESS:
    case XCB_BUTTON_RELEASE:
        platform->inputTime = ((const xcb_button_press_event_t*)event)->time;
        OnButton(platform, (const xcb_button_press_event_t*)event, type == XCB_BUTTON_PRESS);
        return true;
    case XCB_MOTION_NOTIFY:
        OnMotion(platform, (const xcb_motion_notify_event_t*)event);
        return true;
    case XCB_ENTER_NOTIFY:
    case XCB_LEAVE_NOTIFY:
        OnCrossing(platform, (const xcb_enter_notify_event_t*)event, type == XCB_ENTER_NOTIFY);
        return true;
    default:
        return false;
    }
}

void mwinX11FollowIme(mwinX11Platform* platform)
{
    int32_t slot = platform->keyboard.focus;
    const mwinX11Window* window = slot >= 0 ? &platform->windows[slot] : nullptr;
    mwinRect caret = {0};
    if (window == nullptr || !window->textInput)
    {
        mwinImeFocus(&platform->ime, -1, caret);
        return;
    }
    // The input method places its candidates in root coordinates.
    const mwinX11Api* api = &platform->api;
    float scale = platform->scale;
    xcb_translate_coordinates_reply_t* reply = api->translateCoordinatesReply(
        platform->connection,
        api->translateCoordinates(platform->connection, window->window, platform->screen->root,
                                  (int16_t)(window->caret.x * scale),
                                  (int16_t)(window->caret.y * scale)),
        nullptr);
    if (reply != nullptr)
    {
        caret = (mwinRect){(float)reply->dst_x, (float)reply->dst_y, window->caret.width * scale,
                           window->caret.height * scale};
    }
    mwinReleaseSystemMemory(reply);
    mwinImeFocus(&platform->ime, slot, caret);
}

void mwinX11ForgetKeys(mwinX11Platform* platform)
{
    for (size_t i = 0; i < sizeof(platform->keyboard.held); i++)
    {
        platform->keyboard.held[i] = 0;
    }
    mwinXkbResetCompose(&platform->keyboard.xkb);
}

void mwinX11ForgetPointer(mwinX11Platform* platform, uint32_t slot)
{
    if (platform->pointer.focus == (int32_t)slot)
    {
        platform->pointer.focus = -1;
        platform->pointer.buttons = 0;
    }
}

mwinKey mwinX11MapKeyCode(const mwinX11Platform* platform, mwinKeyCode code)
{
    return mwinXkbMapKeyCode(&platform->keyboard.xkb, code);
}

mwinResult mwinX11KeyboardLayout(const mwinX11Platform* platform, char* buffer, size_t capacity,
                                 size_t* lengthOut)
{
    return mwinXkbLayoutName(&platform->keyboard.xkb, buffer, capacity, lengthOut);
}
