// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The activity's display as the one monitor on Android (android.h): its
// name, its whole size in pixels at the configuration's density, its
// physical size from its dots per inch, its refresh rate, and its HDR
// facts (mwin-0036) through the library's Java helper
// maul.window.Screen. HDR output is on while the HDR/SDR ratio
// (Android 14) is above 1, which it is while HDR content shows; the
// headroom is that ratio, unknown where the display can show HDR and
// Android does not tell it. Android tells no SDR white; variable
// refresh is adaptive refresh, which Android 16 tells.

#include "allocator.h"
#include "android.h"

#include <math.h>
#include <string.h>

#define READ_NS 2000000000ull

// Where the facts go in Screen.facts's answer.
enum
{
    factWidth,
    factHeight,
    factXDpi,
    factYDpi,
    factRefresh,
    factHdr,
    factPeak,
    factAverage,
    factRatio,
    factAdaptive,
    factCount,
};

bool mwinAndroidFindScreen(mwinAndroidPlatform* platform, ANativeActivity* activity)
{
    JNIEnv* env = activity->env;
    mwinAndroidScreen* screen = &platform->screen;
    screen->monitor = -1;
    jclass type = mwinAndroidLoadClass(env, activity, "maul.window.Screen");
    if (type == nullptr)
    {
        return false;
    }
    screen->screen = (*env)->NewGlobalRef(env, type);
    screen->readFacts = (*env)->GetStaticMethodID(env, type, "facts", "(Landroid/app/Activity;)[F");
    screen->readName =
        (*env)->GetStaticMethodID(env, type, "name", "(Landroid/app/Activity;)Ljava/lang/String;");
    (*env)->ExceptionClear(env);
    (*env)->DeleteLocalRef(env, type);
    return screen->screen != nullptr && screen->readFacts != nullptr && screen->readName != nullptr;
}

void mwinAndroidStopScreen(mwinAndroidPlatform* platform)
{
    if (platform->screen.screen != nullptr)
    {
        (*platform->java.env)->DeleteGlobalRef(platform->java.env, platform->screen.screen);
        platform->screen.screen = nullptr;
    }
}

// The display's name, cut to the whole characters that fit.
static void ReadName(mwinAndroidPlatform* platform, mwinMonitorInfo* info)
{
    JNIEnv* env = platform->java.env;
    jstring name = (*env)->CallStaticObjectMethod(
        env, platform->screen.screen, platform->screen.readName, platform->activity->clazz);
    if (name == nullptr || (*env)->ExceptionCheck(env))
    {
        (*env)->ExceptionClear(env);
        (*env)->DeleteLocalRef(env, name);
        return;
    }
    size_t size = 0;
    char* bytes = mwinAndroidBytesOf(platform, env, name, &size, nullptr, 0);
    (*env)->DeleteLocalRef(env, name);
    if (bytes == nullptr)
    {
        return;
    }
    size_t length = size;
    if (length > MWIN_MONITOR_NAME_BYTES)
    {
        length = MWIN_MONITOR_NAME_BYTES;
        while (length > 0 && ((unsigned char)bytes[length] & 0xC0) == 0x80)
        {
            --length;
        }
    }
    memcpy(info->name, bytes, length);
    info->nameLength = (uint32_t)length;
    mwinRelease(&platform->context->allocator, bytes, size, 1);
}

// Millimeters across pixels at a density in dots per inch, 0 where it
// is unknown.
static uint32_t MillimetersOf(float pixels, float dpi)
{
    return dpi > 0.0f ? (uint32_t)lroundf(pixels / dpi * 25.4f) : 0;
}

static mwinHdrFacts HdrOf(const jfloat* facts)
{
    float ratio = facts[factRatio];
    mwinHdrFacts hdr = {
        .known = true,
        .active = ratio > 1.0f,
        .peakNits = facts[factPeak],
        .fullFrameNits = facts[factAverage],
    };
    if (ratio >= 1.0f)
    {
        hdr.headroom = ratio;
    }
    else
    {
        hdr.headroom = facts[factHdr] != 0.0f ? 0.0f : 1.0f;
    }
    return hdr;
}

void mwinAndroidReadScreen(mwinAndroidPlatform* platform)
{
    if (platform->activity == nullptr)
    {
        return;
    }
    JNIEnv* env = platform->java.env;
    mwinAndroidScreen* screen = &platform->screen;
    uint64_t nowNs = mwinAndroidNow();
    screen->readNs = nowNs;
    jfloatArray answer = (*env)->CallStaticObjectMethod(env, screen->screen, screen->readFacts,
                                                        platform->activity->clazz);
    jfloat facts[factCount];
    if (answer == nullptr || (*env)->ExceptionCheck(env) ||
        (*env)->GetArrayLength(env, answer) != factCount)
    {
        (*env)->ExceptionClear(env);
        (*env)->DeleteLocalRef(env, answer);
        return;
    }
    (*env)->GetFloatArrayRegion(env, answer, 0, factCount, facts);
    (*env)->DeleteLocalRef(env, answer);
    if (facts[factWidth] < 1.0f || facts[factHeight] < 1.0f)
    {
        return;
    }
    mwinMonitorInfo info = {
        .bounds = {0, 0, (uint32_t)facts[factWidth], (uint32_t)facts[factHeight]},
        .widthMm = MillimetersOf(facts[factWidth], facts[factXDpi]),
        .heightMm = MillimetersOf(facts[factHeight], facts[factYDpi]),
        .scale = mwinAndroidScale(platform),
        .refreshMilliHz = (uint32_t)lroundf(facts[factRefresh] * 1000.0f),
        .variableRefresh = facts[factAdaptive] != 0.0f,
        .primary = true,
        .hdr = HdrOf(facts),
    };
    info.workArea = info.bounds;
    ReadName(platform, &info);
    mwinContext* context = platform->context;
    if (screen->monitor < 0)
    {
        screen->monitor = mwinAddMonitor(context, &info, nowNs);
        mwinAndroidPostDisplay(platform);
    }
    else if (!mwinSameMonitorInfo(&context->monitors[screen->monitor].info, &info))
    {
        mwinChangeMonitor(context, (uint32_t)screen->monitor, &info, nowNs);
    }
}

void mwinAndroidPumpScreen(mwinAndroidPlatform* platform, uint64_t nowNs)
{
    if (nowNs - platform->screen.readNs >= READ_NS)
    {
        mwinAndroidReadScreen(platform);
    }
}

void mwinAndroidPostDisplay(mwinAndroidPlatform* platform)
{
    mwinAndroidWindow* window = &platform->window;
    int32_t monitor = platform->screen.monitor;
    if (platform->slot < 0 || !window->created || window->displayTold || monitor < 0)
    {
        return;
    }
    window->displayTold = true;
    mwinEvent event = {.type = mwin_eventDisplayChanged, .timeNs = mwinAndroidNow()};
    event.data.monitor = mwinMonitorIdOf(platform->context, (uint32_t)monitor);
    mwinPost(platform->context, (uint32_t)platform->slot, &event);
}
