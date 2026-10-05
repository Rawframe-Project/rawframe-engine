// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Gamepads on Android (android.h), through the library's Java helper
// maul.window.Gamepads: InputDevice and its Vibrator are Java only.
//
// - The gamepads are the devices, not virtual, whose sources include
//   the gamepad or the joystick (a keyboard's or a remote's arrows alone
//   are not one); they are looked for every half second, as the other
//   backends' runtimes are, and at once when an event comes from one not
//   followed. A device gone from the list is removed. Their batteries
//   are read when they are looked for (Android 12 and later), a change
//   told as the gamepad's.
// - Their keys and joystick motions come through the activity's input
//   queue, while the window has the focus, and are taken: Android's
//   fallbacks (B as Back) do not follow. A key is a gamepad's by its
//   device and code: a joystick's buttons come from its keyboard source
//   (Android 11 and 15 count BTN_TRIGGER and up as keyboard keys).
//   android_pad_map.c turns them into records.
// - Rumble runs the device's motors (two, heavy then light, where
//   Android 12 lists them; else its one at the stronger): Android times
//   the effect, and the latest call replaces it.

#include "android.h"

#include <string.h>

#define LOOK_NS 500000000ull

static mwinAndroidPad* PadOf(mwinAndroidPads* pads, int32_t device)
{
    for (uint32_t i = 0; i < pads->count; i++)
    {
        if (pads->pads[i].device == device)
        {
            return &pads->pads[i];
        }
    }
    return nullptr;
}

bool mwinAndroidFindPads(mwinAndroidPlatform* platform, ANativeActivity* activity)
{
    JNIEnv* env = activity->env;
    mwinAndroidPads* pads = &platform->pads;
    jclass type = mwinAndroidLoadClass(env, activity, "maul.window.Gamepads");
    jintArray keys = (*env)->NewIntArray(env, MWIN_ANDROID_PAD_KEYS);
    if (type == nullptr || keys == nullptr)
    {
        (*env)->ExceptionClear(env);
        (*env)->DeleteLocalRef(env, type);
        (*env)->DeleteLocalRef(env, keys);
        return false;
    }
    (*env)->SetIntArrayRegion(env, keys, 0, MWIN_ANDROID_PAD_KEYS, mwinAndroidPadKeys);
    pads->type = (*env)->NewGlobalRef(env, type);
    pads->keys = (*env)->NewGlobalRef(env, keys);
    pads->list = (*env)->GetStaticMethodID(env, type, "list", "()[I");
    pads->name = (*env)->GetStaticMethodID(env, type, "name", "(II)[B");
    pads->describe = (*env)->GetStaticMethodID(env, type, "describe", "(I[I)[I");
    pads->ranges = (*env)->GetStaticMethodID(env, type, "ranges", "(I[I)[F");
    pads->battery = (*env)->GetStaticMethodID(env, type, "battery", "(I)I");
    pads->rumble = (*env)->GetStaticMethodID(env, type, "rumble", "(IFFI)Z");
    (*env)->ExceptionClear(env);
    (*env)->DeleteLocalRef(env, type);
    (*env)->DeleteLocalRef(env, keys);
    return pads->type != nullptr && pads->keys != nullptr && pads->list != nullptr &&
           pads->name != nullptr && pads->describe != nullptr && pads->ranges != nullptr &&
           pads->battery != nullptr && pads->rumble != nullptr;
}

static int8_t BatteryOf(const mwinAndroidPlatform* platform, int32_t device)
{
    JNIEnv* env = platform->java.env;
    jint charge =
        (*env)->CallStaticIntMethod(env, platform->pads.type, platform->pads.battery, (jint)device);
    if ((*env)->ExceptionCheck(env))
    {
        (*env)->ExceptionClear(env);
        return -1;
    }
    return charge >= 0 && charge <= 100 ? (int8_t)charge : -1;
}

// Reads what a gamepad is into its layout and facts: false when it is
// gone.
static bool Describe(const mwinAndroidPlatform* platform, int32_t device,
                     mwinAndroidPadLayout* layout, mwinGamepadInfo* info)
{
    JNIEnv* env = platform->java.env;
    const mwinAndroidPads* pads = &platform->pads;
    jintArray facts =
        (*env)->CallStaticObjectMethod(env, pads->type, pads->describe, (jint)device, pads->keys);
    jbyteArray name = facts != nullptr
                          ? (*env)->CallStaticObjectMethod(env, pads->type, pads->name,
                                                           (jint)device, MWIN_GAMEPAD_NAME_BYTES)
                          : nullptr;
    jsize count = facts != nullptr ? (*env)->GetArrayLength(env, facts) : 0;
    bool described = name != nullptr && count >= 5 && !(*env)->ExceptionCheck(env);
    if (described)
    {
        jint head[5];
        (*env)->GetIntArrayRegion(env, facts, 0, 5, head);
        jsize axes = count - 5 < MWIN_ANDROID_PAD_AXES ? count - 5 : MWIN_ANDROID_PAD_AXES;
        *layout = (mwinAndroidPadLayout){
            .keys = (uint64_t)(uint32_t)head[3] | (uint64_t)(uint32_t)head[4] << 32,
            .axisCount = (uint32_t)axes,
        };
        (*env)->GetIntArrayRegion(env, facts, 5, axes, layout->axes);
        jintArray asked = (*env)->NewIntArray(env, axes);
        (*env)->SetIntArrayRegion(env, asked, 0, axes, layout->axes);
        jfloatArray ranges =
            (*env)->CallStaticObjectMethod(env, pads->type, pads->ranges, (jint)device, asked);
        for (jsize i = 0; ranges != nullptr && i < axes; i++)
        {
            jfloat limits[2];
            (*env)->GetFloatArrayRegion(env, ranges, 2 * i, 2, limits);
            layout->min[i] = limits[0];
            layout->max[i] = limits[1];
        }
        (*env)->DeleteLocalRef(env, asked);
        (*env)->DeleteLocalRef(env, ranges);
        jsize length = (*env)->GetArrayLength(env, name);
        *info = (mwinGamepadInfo){
            .nameLength =
                (uint32_t)(length < MWIN_GAMEPAD_NAME_BYTES ? length : MWIN_GAMEPAD_NAME_BYTES),
            .vendor = (uint16_t)head[0],
            .product = (uint16_t)head[1],
            .capabilities = head[2] > 0 ? mwin_padRumble : 0,
        };
        (*env)->GetByteArrayRegion(env, name, 0, (jsize)info->nameLength, (jbyte*)info->name);
        mwinAndroidPadLayoutOf(layout, info);
        described = !(*env)->ExceptionCheck(env);
    }
    (*env)->ExceptionClear(env);
    (*env)->DeleteLocalRef(env, facts);
    (*env)->DeleteLocalRef(env, name);
    return described;
}

static void Add(mwinAndroidPlatform* platform, int32_t device, uint64_t nowNs)
{
    mwinAndroidPads* pads = &platform->pads;
    if (pads->count == MWIN_ANDROID_PADS)
    {
        return;
    }
    mwinAndroidPad* pad = &pads->pads[pads->count];
    mwinGamepadInfo info;
    if (!Describe(platform, device, &pad->layout, &info))
    {
        return;
    }
    info.battery = BatteryOf(platform, device);
    int32_t slot = mwinAddGamepad(platform->context, &info, nowNs);
    if (slot >= 0)
    {
        pad->device = device;
        pad->slot = (uint32_t)slot;
        pad->battery = info.battery;
        pads->count += 1;
    }
}

static void Remove(mwinAndroidPlatform* platform, uint32_t index, uint64_t nowNs)
{
    mwinAndroidPads* pads = &platform->pads;
    mwinRemoveGamepad(platform->context, pads->pads[index].slot, nowNs);
    pads->count -= 1;
    pads->pads[index] = pads->pads[pads->count];
}

static bool Listed(const jint* ids, jsize count, int32_t device)
{
    for (jsize i = 0; i < count; i++)
    {
        if (ids[i] == device)
        {
            return true;
        }
    }
    return false;
}

// The gamepads connected now: the gone removed, the new added, a changed
// battery told.
static void Look(mwinAndroidPlatform* platform, uint64_t nowNs)
{
    JNIEnv* env = platform->java.env;
    mwinAndroidPads* pads = &platform->pads;
    pads->lookedNs = nowNs;
    jintArray list = (*env)->CallStaticObjectMethod(env, pads->type, pads->list);
    if (list == nullptr || (*env)->ExceptionCheck(env))
    {
        (*env)->ExceptionClear(env);
        (*env)->DeleteLocalRef(env, list);
        return;
    }
    jint ids[MWIN_ANDROID_PADS * 2];
    jsize count = (*env)->GetArrayLength(env, list);
    count = count < MWIN_ANDROID_PADS * 2 ? count : MWIN_ANDROID_PADS * 2;
    (*env)->GetIntArrayRegion(env, list, 0, count, ids);
    (*env)->DeleteLocalRef(env, list);
    for (uint32_t i = pads->count; i-- > 0;)
    {
        if (!Listed(ids, count, pads->pads[i].device))
        {
            Remove(platform, i, nowNs);
        }
    }
    for (jsize i = 0; i < count; i++)
    {
        mwinAndroidPad* pad = PadOf(pads, ids[i]);
        if (pad == nullptr)
        {
            Add(platform, ids[i], nowNs);
            continue;
        }
        int8_t battery = BatteryOf(platform, pad->device);
        if (battery != pad->battery)
        {
            pad->battery = battery;
            mwinGamepadInfo info = platform->context->gamepads[pad->slot].info;
            info.battery = battery;
            mwinChangeGamepad(platform->context, pad->slot, &info, nowNs);
        }
    }
}

void mwinAndroidPumpPads(mwinAndroidPlatform* platform, uint64_t nowNs)
{
    if (nowNs - platform->pads.lookedNs >= LOOK_NS)
    {
        Look(platform, nowNs);
    }
}

static bool FromPad(int32_t source)
{
    return (source & AINPUT_SOURCE_GAMEPAD) == AINPUT_SOURCE_GAMEPAD ||
           (source & AINPUT_SOURCE_JOYSTICK) == AINPUT_SOURCE_JOYSTICK;
}

// A joystick motion's samples, its history first.
static void Motion(mwinAndroidPlatform* platform, mwinAndroidPad* pad, const AInputEvent* event)
{
    mwinAndroidPadLayout* layout = &pad->layout;
    float values[MWIN_ANDROID_PAD_AXES];
    size_t history = AMotionEvent_getHistorySize(event);
    for (size_t past = 0; past <= history; past++)
    {
        bool now = past == history;
        for (uint32_t i = 0; i < layout->axisCount; i++)
        {
            values[i] = now ? AMotionEvent_getAxisValue(event, layout->axes[i], 0)
                            : AMotionEvent_getHistoricalAxisValue(event, layout->axes[i], 0, past);
        }
        int64_t timeNs = now ? AMotionEvent_getEventTime(event)
                             : AMotionEvent_getHistoricalEventTime(event, past);
        mwinAndroidPadAxes(platform->context, pad->slot, layout, values, (uint64_t)timeNs);
    }
}

bool mwinAndroidPadInput(mwinAndroidPlatform* platform, const AInputEvent* event)
{
    // A joystick's buttons come from its keyboard source: a key is a
    // gamepad's by its device and code, whatever the source says.
    bool key = AInputEvent_getType(event) == AINPUT_EVENT_TYPE_KEY;
    int32_t source = AInputEvent_getSource(event);
    if (key ? !mwinAndroidIsPadKey(AKeyEvent_getKeyCode(event)) : !FromPad(source))
    {
        return false;
    }
    int32_t device = AInputEvent_getDeviceId(event);
    mwinAndroidPad* pad = PadOf(&platform->pads, device);
    if (pad == nullptr && FromPad(source))
    {
        // A gamepad that came since the last look.
        Look(platform, mwinAndroidNow());
        pad = PadOf(&platform->pads, device);
    }
    if (pad == nullptr)
    {
        return false;
    }
    if (key)
    {
        int32_t action = AKeyEvent_getAction(event);
        if (action != AKEY_EVENT_ACTION_MULTIPLE && AKeyEvent_getRepeatCount(event) == 0)
        {
            mwinAndroidPadKey(platform->context, pad->slot, &pad->layout,
                              AKeyEvent_getKeyCode(event), action == AKEY_EVENT_ACTION_DOWN,
                              (uint64_t)AKeyEvent_getEventTime(event));
        }
        return true;
    }
    if ((source & AINPUT_SOURCE_JOYSTICK) == AINPUT_SOURCE_JOYSTICK &&
        (AMotionEvent_getAction(event) & AMOTION_EVENT_ACTION_MASK) == AMOTION_EVENT_ACTION_MOVE)
    {
        Motion(platform, pad, event);
        return true;
    }
    return false;
}

// Runs a device's motors: false when it has none or Java failed.
static bool Vibrate(const mwinAndroidPlatform* platform, int32_t device, float low, float high,
                    uint32_t durationMs)
{
    JNIEnv* env = platform->java.env;
    // As values: floats through the variadic call would become doubles.
    jvalue arguments[4] = {{.i = device},
                           {.f = low},
                           {.f = high},
                           {.i = durationMs > INT32_MAX ? INT32_MAX : (jint)durationMs}};
    jboolean ran = (*env)->CallStaticBooleanMethodA(env, platform->pads.type, platform->pads.rumble,
                                                    arguments);
    bool thrown = (*env)->ExceptionCheck(env);
    (*env)->ExceptionClear(env);
    return ran && !thrown;
}

mwinResult mwinAndroidRumble(mwinAndroidPlatform* platform, uint32_t slot, float low, float high,
                             uint32_t durationMs)
{
    const mwinAndroidPads* pads = &platform->pads;
    for (uint32_t i = 0; i < pads->count; i++)
    {
        if (pads->pads[i].slot == slot)
        {
            return Vibrate(platform, pads->pads[i].device, low, high, durationMs)
                       ? mwin_success
                       : mwin_errorPlatform;
        }
    }
    return mwin_errorStale;
}

void mwinAndroidStopPads(mwinAndroidPlatform* platform)
{
    JNIEnv* env = platform->java.env;
    mwinAndroidPads* pads = &platform->pads;
    for (uint32_t i = 0; pads->type != nullptr && i < pads->count; i++)
    {
        if ((platform->context->gamepads[pads->pads[i].slot].info.capabilities & mwin_padRumble) !=
            0)
        {
            (void)Vibrate(platform, pads->pads[i].device, 0.0f, 0.0f, 0);
        }
    }
    if (pads->type != nullptr)
    {
        (*env)->DeleteGlobalRef(env, pads->type);
    }
    if (pads->keys != nullptr)
    {
        (*env)->DeleteGlobalRef(env, pads->keys);
    }
    memset(pads, 0, sizeof(*pads));
}
