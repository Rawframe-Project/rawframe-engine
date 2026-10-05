// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The system's preferences and facts on Android (android.h): the light
// or dark theme from the configuration's night mode; through the
// library's Java helper maul.window.Facts, the text scale (the
// configuration's font scale), reduced motion (Android's "remove
// animations" sets the animator scale to 0), the accent (Android 12's
// system palette; none before), battery saver, whether the battery
// provides the power, and the preferred locales.
//
// They are read when an activity starts (one joining the program
// starts too, and settings changed in the system's settings application
// come back with it) and when the configuration changes, and the power
// every two seconds, Android announcing it only to receivers; the core
// posts what changed.

#include "allocator.h"
#include "android.h"

#define POWER_NS 2000000000ull

bool mwinAndroidFindFacts(mwinAndroidPlatform* platform, ANativeActivity* activity)
{
    JNIEnv* env = activity->env;
    mwinAndroidJava* java = &platform->java;
    jclass type = mwinAndroidLoadClass(env, activity, "maul.window.Facts");
    if (type == nullptr)
    {
        return false;
    }
    java->facts = (*env)->NewGlobalRef(env, type);
    java->readFacts = (*env)->GetStaticMethodID(env, type, "facts", "(Landroid/app/Activity;)[I");
    java->readLocales = (*env)->GetStaticMethodID(env, type, "locales", "()Ljava/lang/String;");
    (*env)->ExceptionClear(env);
    (*env)->DeleteLocalRef(env, type);
    return java->facts != nullptr && java->readFacts != nullptr && java->readLocales != nullptr;
}

void mwinAndroidStopFacts(mwinAndroidPlatform* platform)
{
    if (platform->java.facts != nullptr)
    {
        (*platform->java.env)->DeleteGlobalRef(platform->java.env, platform->java.facts);
        platform->java.facts = nullptr;
    }
}

static mwinTristate TristateOf(jint said)
{
    return said < 0 ? mwin_unknown : said > 0 ? mwin_yes : mwin_no;
}

static void ReadFacts(mwinAndroidPlatform* platform, uint64_t nowNs)
{
    JNIEnv* env = platform->java.env;
    jintArray answer = (*env)->CallStaticObjectMethod(
        env, platform->java.facts, platform->java.readFacts, platform->activity->clazz);
    jint said[6];
    if (answer == nullptr || (*env)->ExceptionCheck(env) ||
        (*env)->GetArrayLength(env, answer) != 6)
    {
        (*env)->ExceptionClear(env);
        (*env)->DeleteLocalRef(env, answer);
        return;
    }
    (*env)->GetIntArrayRegion(env, answer, 0, 6, said);
    (*env)->DeleteLocalRef(env, answer);
    int32_t night = AConfiguration_getUiModeNight(platform->configuration);
    uint32_t argb = (uint32_t)said[3];
    mwinSystemFacts facts = {
        .theme = night == ACONFIGURATION_UI_MODE_NIGHT_YES  ? mwin_themeDark
                 : night == ACONFIGURATION_UI_MODE_NIGHT_NO ? mwin_themeLight
                                                            : mwin_themeUnknown,
        .hasAccent = said[2] != 0,
        // ARGB to RRGGBBAA.
        .accent = said[2] != 0 ? argb << 8 | argb >> 24 : 0,
        .reducedMotion = said[1] != 0,
        .textScale = said[0] > 0 ? (float)said[0] / 1000.0f : 1.0f,
        .onBattery = TristateOf(said[5]),
        .lowPower = said[4] != 0 ? mwin_yes : mwin_no,
    };
    mwinSetSystemFacts(platform->context, &facts, nowNs);
}

static void ReadLocales(mwinAndroidPlatform* platform, uint64_t nowNs)
{
    JNIEnv* env = platform->java.env;
    jstring tags =
        (*env)->CallStaticObjectMethod(env, platform->java.facts, platform->java.readLocales);
    if (tags == nullptr || (*env)->ExceptionCheck(env))
    {
        (*env)->ExceptionClear(env);
        (*env)->DeleteLocalRef(env, tags);
        return;
    }
    size_t size = 0;
    char* bytes = mwinAndroidBytesOf(platform, env, tags, &size, nullptr, 0);
    (void)mwinSetLocales(platform->context, bytes, bytes != nullptr ? size : 0, nowNs);
    if (bytes != nullptr)
    {
        mwinRelease(&platform->context->allocator, bytes, size, 1);
    }
    (*env)->DeleteLocalRef(env, tags);
}

void mwinAndroidReadSystem(mwinAndroidPlatform* platform)
{
    if (platform->activity != nullptr)
    {
        uint64_t nowNs = mwinAndroidNow();
        platform->factsReadNs = nowNs;
        ReadFacts(platform, nowNs);
        ReadLocales(platform, nowNs);
    }
}

void mwinAndroidPumpFacts(mwinAndroidPlatform* platform, uint64_t nowNs)
{
    if (platform->activity != nullptr && nowNs - platform->factsReadNs >= POWER_NS)
    {
        platform->factsReadNs = nowNs;
        ReadFacts(platform, nowNs);
    }
}
