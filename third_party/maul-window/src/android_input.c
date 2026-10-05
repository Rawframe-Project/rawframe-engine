// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Input from the activity's input queue on Android (android.h).
//
// - A motion event becomes samples, its history first, for the pointers
//   (android_motion.h), in logical units.
// - A key is the key of its scan code where a keyboard gives one (the
//   Linux input code, as on Linux), else the key of its Android key code
//   through Android's generic layout (the on-screen keyboard's keys,
//   injected ones). Its meaning is what the device's key character map
//   types for it without modifiers, known once it was pressed under the
//   current layout, as on iOS; a key that types nothing is named. A press
//   also types its text, unless Control or Meta is held; a dead key's
//   accent joins the next character.
// - The keys Android calls system keys (KeyEvent.isSystemKey: back,
//   home, volume, media and the like) stay the system's, as do keys with
//   no code. The program takes the rest, and every motion while it has a
//   window, so that Android acts on none of them: an Escape left to it
//   would go back.

#include "android.h"
#include "android_keys.h"
#include "evdev.h"
#include "key_prints.h"

#include "maul-unicode/encoding.h"

#include <string.h>

static_assert(mwin_androidPrimary == AMOTION_EVENT_BUTTON_PRIMARY &&
                  mwin_androidSecondary == AMOTION_EVENT_BUTTON_SECONDARY &&
                  mwin_androidTertiary == AMOTION_EVENT_BUTTON_TERTIARY &&
                  mwin_androidBack == AMOTION_EVENT_BUTTON_BACK &&
                  mwin_androidForward == AMOTION_EVENT_BUTTON_FORWARD &&
                  mwin_androidStylusPrimary == AMOTION_EVENT_BUTTON_STYLUS_PRIMARY,
              "the motion samples take Android's button values");

// KeyCharacterMap's flag on a dead key's accent.
#define COMBINING_ACCENT 0x80000000u

bool mwinAndroidFindInput(mwinAndroidPlatform* platform)
{
    JNIEnv* env = platform->java.env;
    mwinAndroidKeys* keys = &platform->keys;
    jclass maps = (*env)->FindClass(env, "android/view/KeyCharacterMap");
    jclass view = (*env)->FindClass(env, "android/view/ViewConfiguration");
    if (maps == nullptr || view == nullptr)
    {
        (*env)->ExceptionClear(env);
        return false;
    }
    keys->maps = (*env)->NewGlobalRef(env, maps);
    keys->load = (*env)->GetStaticMethodID(env, maps, "load", "(I)Landroid/view/KeyCharacterMap;");
    keys->get = (*env)->GetMethodID(env, maps, "get", "(II)I");
    keys->deadChar = (*env)->GetStaticMethodID(env, maps, "getDeadChar", "(II)I");
    jmethodID timeout = (*env)->GetStaticMethodID(env, view, "getDoubleTapTimeout", "()I");
    jint milliseconds = timeout != nullptr ? (*env)->CallStaticIntMethod(env, view, timeout) : 0;
    platform->pointers.doubleClickNs =
        milliseconds > 0 ? (uint64_t)milliseconds * 1000000u : MWIN_DOUBLE_CLICK_NS;
    (*env)->DeleteLocalRef(env, maps);
    (*env)->DeleteLocalRef(env, view);
    bool found = keys->load != nullptr && keys->get != nullptr && keys->deadChar != nullptr;
    (*env)->ExceptionClear(env);
    return found;
}

void mwinAndroidLoseInput(mwinAndroidPlatform* platform)
{
    if (platform->keys.maps != nullptr)
    {
        (*platform->java.env)->DeleteGlobalRef(platform->java.env, platform->keys.maps);
        platform->keys.maps = nullptr;
    }
}

static mwinModifiers ModifiersOf(int32_t meta)
{
    return (mwinModifiers)(((meta & AMETA_SHIFT_ON) != 0 ? mwin_modShift : 0) |
                           ((meta & AMETA_CTRL_ON) != 0 ? mwin_modControl : 0) |
                           ((meta & AMETA_ALT_ON) != 0 ? mwin_modAlt : 0) |
                           ((meta & AMETA_META_ON) != 0 ? mwin_modMeta : 0) |
                           ((meta & AMETA_CAPS_LOCK_ON) != 0 ? mwin_modCapsLock : 0) |
                           ((meta & AMETA_NUM_LOCK_ON) != 0 ? mwin_modNumLock : 0));
}

static mwinAndroidAction ActionOf(int32_t masked)
{
    switch (masked)
    {
    case AMOTION_EVENT_ACTION_DOWN:
        return mwin_androidDown;
    case AMOTION_EVENT_ACTION_UP:
        return mwin_androidUp;
    case AMOTION_EVENT_ACTION_MOVE:
        return mwin_androidMove;
    case AMOTION_EVENT_ACTION_CANCEL:
        return mwin_androidCancel;
    case AMOTION_EVENT_ACTION_POINTER_DOWN:
        return mwin_androidPointerDown;
    case AMOTION_EVENT_ACTION_POINTER_UP:
        return mwin_androidPointerUp;
    case AMOTION_EVENT_ACTION_HOVER_ENTER:
        return mwin_androidHoverEnter;
    case AMOTION_EVENT_ACTION_HOVER_MOVE:
        return mwin_androidHoverMove;
    case AMOTION_EVENT_ACTION_HOVER_EXIT:
        return mwin_androidHoverExit;
    case AMOTION_EVENT_ACTION_SCROLL:
        return mwin_androidScroll;
    case AMOTION_EVENT_ACTION_BUTTON_PRESS:
        return mwin_androidButtonPress;
    case AMOTION_EVENT_ACTION_BUTTON_RELEASE:
        return mwin_androidButtonRelease;
    default:
        return mwin_androidOther;
    }
}

static mwinAndroidTool ToolOf(int32_t tool)
{
    switch (tool)
    {
    case AMOTION_EVENT_TOOL_TYPE_STYLUS:
        return mwin_androidStylus;
    case AMOTION_EVENT_TOOL_TYPE_ERASER:
        return mwin_androidEraser;
    case AMOTION_EVENT_TOOL_TYPE_MOUSE:
        return mwin_androidMouse;
    default:
        return mwin_androidFinger;
    }
}

// The scale positions are divided by: the window's, else the density's.
static float ScaleOf(const mwinAndroidPlatform* platform)
{
    return platform->window.scale > 0.0f ? platform->window.scale : mwinAndroidScale(platform);
}

// The pointers of a sample: a past one of the history, or the event's
// own (SIZE_MAX).
static void ReadPointers(const AInputEvent* event, size_t past, float scale,
                         mwinAndroidMotion* motion)
{
    size_t count = AMotionEvent_getPointerCount(event);
    motion->count = (uint32_t)(count < MWIN_ANDROID_POINTERS ? count : MWIN_ANDROID_POINTERS);
    for (size_t i = 0; i < motion->count; i++)
    {
        mwinAndroidPointer* pointer = &motion->pointers[i];
        pointer->id = AMotionEvent_getPointerId(event, i);
        pointer->tool = ToolOf(AMotionEvent_getToolType(event, i));
        if (past == SIZE_MAX)
        {
            pointer->position = (mwinPosition){AMotionEvent_getX(event, i) / scale,
                                               AMotionEvent_getY(event, i) / scale};
            pointer->pressure = AMotionEvent_getPressure(event, i);
            pointer->tilt = AMotionEvent_getAxisValue(event, AMOTION_EVENT_AXIS_TILT, i);
            pointer->orientation = AMotionEvent_getOrientation(event, i);
        }
        else
        {
            pointer->position = (mwinPosition){AMotionEvent_getHistoricalX(event, i, past) / scale,
                                               AMotionEvent_getHistoricalY(event, i, past) / scale};
            pointer->pressure = AMotionEvent_getHistoricalPressure(event, i, past);
            pointer->tilt =
                AMotionEvent_getHistoricalAxisValue(event, AMOTION_EVENT_AXIS_TILT, i, past);
            pointer->orientation = AMotionEvent_getHistoricalOrientation(event, i, past);
        }
    }
}

static void Motion(mwinAndroidPlatform* platform, const AInputEvent* event)
{
    int32_t action = AMotionEvent_getAction(event);
    int32_t source = AInputEvent_getSource(event);
    float scale = ScaleOf(platform);
    mwinAndroidMotion motion = {
        .action = ActionOf(action & AMOTION_EVENT_ACTION_MASK),
        .index = (uint32_t)((action & AMOTION_EVENT_ACTION_POINTER_INDEX_MASK) >>
                            AMOTION_EVENT_ACTION_POINTER_INDEX_SHIFT),
        .buttons = (uint32_t)AMotionEvent_getButtonState(event),
        .modifiers = ModifiersOf(AMotionEvent_getMetaState(event)),
    };
    motion.mouse = (source & AINPUT_SOURCE_MOUSE) == AINPUT_SOURCE_MOUSE ||
                   AMotionEvent_getToolType(event, 0) == AMOTION_EVENT_TOOL_TYPE_MOUSE;
    motion.scroll =
        (mwinWheelEvent){AMotionEvent_getAxisValue(event, AMOTION_EVENT_AXIS_HSCROLL, 0),
                         AMotionEvent_getAxisValue(event, AMOTION_EVENT_AXIS_VSCROLL, 0)};
    uint32_t slot = (uint32_t)platform->slot;
    // A move's history comes first, each sample a move of its own time.
    if (motion.action == mwin_androidMove || motion.action == mwin_androidHoverMove)
    {
        size_t history = AMotionEvent_getHistorySize(event);
        for (size_t past = 0; past < history; past++)
        {
            ReadPointers(event, past, scale, &motion);
            motion.timeNs = (uint64_t)AMotionEvent_getHistoricalEventTime(event, past);
            mwinAndroidMotionSample(platform->context, slot, &platform->pointers, &motion);
        }
    }
    ReadPointers(event, SIZE_MAX, scale, &motion);
    motion.timeNs = (uint64_t)AMotionEvent_getEventTime(event);
    mwinAndroidMotionSample(platform->context, slot, &platform->pointers, &motion);
}

// The keys Android keeps for itself, as KeyEvent.isSystemKey lists them,
// but Back, which is the window's close request.
static bool IsSystemKey(int32_t keyCode)
{
    switch (keyCode)
    {
    case AKEYCODE_MENU:
    case AKEYCODE_SOFT_RIGHT:
    case AKEYCODE_HOME:
    case AKEYCODE_RECENT_APPS:
    case AKEYCODE_CALL:
    case AKEYCODE_ENDCALL:
    case AKEYCODE_VOLUME_UP:
    case AKEYCODE_VOLUME_DOWN:
    case AKEYCODE_VOLUME_MUTE:
    case AKEYCODE_MUTE:
    case AKEYCODE_POWER:
    case AKEYCODE_HEADSETHOOK:
    case AKEYCODE_MEDIA_PLAY:
    case AKEYCODE_MEDIA_PAUSE:
    case AKEYCODE_MEDIA_PLAY_PAUSE:
    case AKEYCODE_MEDIA_STOP:
    case AKEYCODE_MEDIA_NEXT:
    case AKEYCODE_MEDIA_PREVIOUS:
    case AKEYCODE_MEDIA_REWIND:
    case AKEYCODE_MEDIA_RECORD:
    case AKEYCODE_MEDIA_FAST_FORWARD:
    case AKEYCODE_CAMERA:
    case AKEYCODE_FOCUS:
    case AKEYCODE_SEARCH:
    case AKEYCODE_BRIGHTNESS_DOWN:
    case AKEYCODE_BRIGHTNESS_UP:
    case AKEYCODE_KEYBOARD_BACKLIGHT_DOWN:
    case AKEYCODE_KEYBOARD_BACKLIGHT_UP:
    case AKEYCODE_KEYBOARD_BACKLIGHT_TOGGLE:
    case AKEYCODE_MEDIA_AUDIO_TRACK:
    case AKEYCODE_SYSTEM_NAVIGATION_UP:
    case AKEYCODE_SYSTEM_NAVIGATION_DOWN:
    case AKEYCODE_SYSTEM_NAVIGATION_LEFT:
    case AKEYCODE_SYSTEM_NAVIGATION_RIGHT:
    case AKEYCODE_STEM_PRIMARY:
        return true;
    default:
        return false;
    }
}

// What the device's key character map types for a key under modifiers
// (a dead key's accent with COMBINING_ACCENT set), 0 for nothing; the
// virtual keyboard's map for a device that is gone.
static uint32_t Typed(const mwinAndroidPlatform* platform, int32_t device, int32_t keyCode,
                      int32_t meta)
{
    JNIEnv* env = platform->java.env;
    const mwinAndroidKeys* keys = &platform->keys;
    jobject map = (*env)->CallStaticObjectMethod(env, keys->maps, keys->load, device);
    if ((*env)->ExceptionCheck(env))
    {
        (*env)->ExceptionClear(env);
        map = (*env)->CallStaticObjectMethod(env, keys->maps, keys->load, -1);
    }
    jint typed = map != nullptr ? (*env)->CallIntMethod(env, map, keys->get, keyCode, meta) : 0;
    (*env)->ExceptionClear(env);
    (*env)->DeleteLocalRef(env, map);
    return (uint32_t)typed;
}

static uint32_t Combined(const mwinAndroidPlatform* platform, int32_t accent, uint32_t typed)
{
    JNIEnv* env = platform->java.env;
    jint combined = (*env)->CallStaticIntMethod(env, platform->keys.maps, platform->keys.deadChar,
                                                accent, (jint)typed);
    (*env)->ExceptionClear(env);
    return combined > 0 ? (uint32_t)combined : typed;
}

static bool IsPrinting(uint32_t typed)
{
    return typed >= 0x20 && typed != 0x7F && (typed & COMBINING_ACCENT) == 0;
}

static void PostText(mwinAndroidPlatform* platform, uint32_t typed, uint64_t timeNs)
{
    char bytes[4];
    size_t size = 0;
    if (muniEncodeUtf8(typed, bytes, &size) != muni_success)
    {
        return;
    }
    mwinEvent event = {.type = mwin_eventTextInput, .timeNs = timeNs};
    event.data.text = (mwinTextEvent){bytes, (uint32_t)size};
    mwinPost(platform->context, (uint32_t)platform->slot, &event);
}

// The text a press types: a dead key's accent waits for the next key.
static void Type(mwinAndroidPlatform* platform, const mwinAndroidKey* key)
{
    if ((key->meta & (AMETA_CTRL_ON | AMETA_META_ON)) != 0)
    {
        return;
    }
    uint32_t typed = Typed(platform, key->device, key->keyCode, key->meta);
    mwinAndroidKeys* keys = &platform->keys;
    if ((typed & COMBINING_ACCENT) != 0)
    {
        keys->accent = (int32_t)(typed & ~COMBINING_ACCENT);
        return;
    }
    if (keys->accent != 0 && IsPrinting(typed))
    {
        typed = Combined(platform, keys->accent, typed);
    }
    keys->accent = 0;
    if (IsPrinting(typed))
    {
        PostText(platform, typed, key->timeNs);
    }
}

static mwinKeyCode CodeOf(const mwinAndroidKey* key)
{
    mwinKeyCode code =
        key->scanCode > 0 ? mwinKeyCodeFromEvdev((uint32_t)key->scanCode) : mwin_codeUnknown;
    return code != mwin_codeUnknown ? code : mwinKeyCodeFromEvdev(mwinAndroidEvdevOf(key->keyCode));
}

bool mwinAndroidKeyEvent(mwinAndroidPlatform* platform, const mwinAndroidKey* key)
{
    mwinKeyCode code = CodeOf(key);
    if (platform->slot < 0 || !platform->window.created || IsSystemKey(key->keyCode) ||
        code == mwin_codeUnknown || code >= MWIN_ANDROID_KEY_CODES ||
        key->action == AKEY_EVENT_ACTION_MULTIPLE)
    {
        return false;
    }
    mwinAndroidKeys* keys = &platform->keys;
    bool down = key->action == AKEY_EVENT_ACTION_DOWN;
    uint8_t bit = (uint8_t)(1u << (code % 8));
    bool held = (keys->held[code / 8] & bit) != 0;
    mwinKey meaning = MWIN_KEY_NAMED | code;
    if (mwinKeyPrints(code))
    {
        uint32_t typed = Typed(platform, key->device, key->keyCode, 0);
        meaning = IsPrinting(typed) ? typed : keys->meanings[code];
        keys->meanings[code] = meaning;
    }
    // A release of a key no press was seen of is left out.
    if (!down && !held)
    {
        return true;
    }
    keys->held[code / 8] = down ? (uint8_t)(keys->held[code / 8] | bit)
                                : (uint8_t)(keys->held[code / 8] & (uint8_t)~bit);
    mwinEvent record = {.type = down ? mwin_eventKeyDown : mwin_eventKeyUp, .timeNs = key->timeNs};
    record.data.key = (mwinKeyEvent){code, ModifiersOf(key->meta), meaning, down && key->repeat};
    mwinPost(platform->context, (uint32_t)platform->slot, &record);
    if (down)
    {
        Type(platform, key);
    }
    return true;
}

// Back, a key or the gesture Android turns into one, asks the window to
// close when it is let go, unless Android cancelled it: the program
// decides, ending (Back at the root leaves the application) or going
// back within itself.
static void Back(mwinAndroidPlatform* platform, const AInputEvent* event)
{
    bool cancelled = (AKeyEvent_getFlags(event) & AKEY_EVENT_FLAG_CANCELED) != 0;
    if (AKeyEvent_getAction(event) == AKEY_EVENT_ACTION_UP && !cancelled)
    {
        mwinEvent close = {.type = mwin_eventCloseRequested,
                           .timeNs = (uint64_t)AKeyEvent_getEventTime(event)};
        mwinPost(platform->context, (uint32_t)platform->slot, &close);
    }
}

bool mwinAndroidInput(mwinAndroidPlatform* platform, const AInputEvent* event)
{
    if (platform->slot < 0 || !platform->window.created)
    {
        return false;
    }
#ifdef MAUL_WINDOW_GAMEPAD
    if (mwinAndroidPadInput(platform, event))
    {
        return true;
    }
#endif
    switch (AInputEvent_getType(event))
    {
    case AINPUT_EVENT_TYPE_KEY:
    {
        if (AKeyEvent_getKeyCode(event) == AKEYCODE_BACK)
        {
            Back(platform, event);
            return true;
        }
        mwinAndroidKey key = {
            .action = AKeyEvent_getAction(event),
            .keyCode = AKeyEvent_getKeyCode(event),
            .scanCode = AKeyEvent_getScanCode(event),
            .meta = AKeyEvent_getMetaState(event),
            .device = AInputEvent_getDeviceId(event),
            .repeat = AKeyEvent_getRepeatCount(event) > 0,
            .timeNs = (uint64_t)AKeyEvent_getEventTime(event),
        };
        return mwinAndroidKeyEvent(platform, &key);
    }
    case AINPUT_EVENT_TYPE_MOTION:
        // Touch exploration's hovers are the accessibility tree's.
        if (!mwinAndroidExplore(platform, event))
        {
            Motion(platform, event);
        }
        return true;
    default:
        return false;
    }
}

mwinKey mwinAndroidMapKeyCode(const mwinAndroidPlatform* platform, mwinKeyCode code)
{
    if (code >= MWIN_ANDROID_KEY_CODES)
    {
        return 0;
    }
    return mwinKeyPrints(code) ? platform->keys.meanings[code] : MWIN_KEY_NAMED | code;
}

void mwinAndroidForgetInput(mwinAndroidPlatform* platform)
{
    memset(platform->keys.held, 0, sizeof(platform->keys.held));
    platform->keys.accent = 0;
    mwinAndroidForgetPointers(&platform->pointers);
}
