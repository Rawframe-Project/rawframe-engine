// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The clipboard and services on Android (android.h), through the
// library's Java helper maul.window.Services where Android has them in
// Java only.
//
// - The clipboard holds text: a write puts it there as plain text, and a
//   read takes the first item as text (`coerceToText`), at once. From
//   Android 10 only the focused application reads it; a refused read,
//   which Android tells apart only in its log, reads as empty, as on iOS.
// - An address opens in the application the user chose for it (an
//   `ACTION_VIEW` intent), answered at once: failed when there is none.
// - The display stays awake through the activity's window flag
//   (`FLAG_KEEP_SCREEN_ON`), which holds while the window shows; an
//   activity that joins the program later gets it too.
// - Android has no file manager to show a file in, and no message box a
//   program could wait for on its main thread (message_box.c answers
//   that there is none).

#include "allocator.h"
#include "android.h"

#include <android/window.h>

bool mwinAndroidFindServices(mwinAndroidPlatform* platform, ANativeActivity* activity)
{
    JNIEnv* env = activity->env;
    mwinAndroidJava* java = &platform->java;
    jclass type = mwinAndroidLoadClass(env, activity, "maul.window.Services");
    if (type == nullptr)
    {
        return false;
    }
    java->services = (*env)->NewGlobalRef(env, type);
    java->writeClipboard =
        (*env)->GetStaticMethodID(env, type, "writeClipboard", "(Landroid/app/Activity;[B)Z");
    java->readClipboard = (*env)->GetStaticMethodID(env, type, "readClipboard",
                                                    "(Landroid/app/Activity;)Ljava/lang/String;");
    java->openUrl = (*env)->GetStaticMethodID(env, type, "openUrl", "(Landroid/app/Activity;[B)Z");
    (*env)->ExceptionClear(env);
    (*env)->DeleteLocalRef(env, type);
    return java->services != nullptr && java->writeClipboard != nullptr &&
           java->readClipboard != nullptr && java->openUrl != nullptr;
}

void mwinAndroidStopServices(mwinAndroidPlatform* platform)
{
    if (platform->java.services != nullptr)
    {
        (*platform->java.env)->DeleteGlobalRef(platform->java.env, platform->java.services);
        platform->java.services = nullptr;
    }
}

// Calls a helper taking the activity and bytes, answering a boolean.
static mwinOutcome CallWithBytes(const mwinAndroidPlatform* platform, jmethodID method,
                                 const char* bytes, uint32_t length)
{
    if (platform->activity == nullptr)
    {
        return mwin_outcomeFailed;
    }
    JNIEnv* env = platform->java.env;
    jbyteArray array = (*env)->NewByteArray(env, (jsize)length);
    if (array == nullptr)
    {
        (*env)->ExceptionClear(env);
        return mwin_outcomeFailed;
    }
    (*env)->SetByteArrayRegion(env, array, 0, (jsize)length, (const jbyte*)bytes);
    jboolean done = (*env)->CallStaticBooleanMethod(env, platform->java.services, method,
                                                    platform->activity->clazz, array);
    bool thrown = (*env)->ExceptionCheck(env);
    (*env)->ExceptionClear(env);
    (*env)->DeleteLocalRef(env, array);
    return done && !thrown ? mwin_outcomeDone : mwin_outcomeFailed;
}

mwinOutcome mwinAndroidWriteClipboard(const mwinAndroidPlatform* platform)
{
    const mwinContext* context = platform->context;
    return CallWithBytes(platform, platform->java.writeClipboard, context->clipboardOffer,
                         context->clipboardOfferLength);
}

mwinOutcome mwinAndroidReadClipboard(const mwinAndroidPlatform* platform)
{
    mwinContext* context = platform->context;
    if (platform->activity == nullptr)
    {
        return mwin_outcomeFailed;
    }
    JNIEnv* env = platform->java.env;
    jstring text = (*env)->CallStaticObjectMethod(
        env, platform->java.services, platform->java.readClipboard, platform->activity->clazz);
    if (text == nullptr || (*env)->ExceptionCheck(env))
    {
        (*env)->ExceptionClear(env);
        return mwin_outcomeFailed;
    }
    // Each unit is at least a byte of UTF-8: text past the limit is not
    // converted.
    jsize units = (*env)->GetStringLength(env, text);
    mwinOutcome outcome = mwin_outcomeTooLarge;
    if ((uint64_t)units <= context->limits.clipboardBytes)
    {
        size_t size = 0;
        char* bytes = mwinAndroidBytesOf(platform, env, text, &size, nullptr, 0);
        outcome = units == 0 || bytes != nullptr ? mwinTakeClipboardText(context, bytes, size)
                                                 : mwin_outcomeFailed;
        if (bytes != nullptr)
        {
            mwinRelease(&context->allocator, bytes, size, 1);
        }
    }
    (*env)->DeleteLocalRef(env, text);
    return outcome;
}

mwinOutcome mwinAndroidOpenUrl(const mwinAndroidPlatform* platform, const mwinRequest* request)
{
    return CallWithBytes(platform, platform->java.openUrl, request->value.text.bytes,
                         request->value.text.length);
}

void mwinAndroidApplyAwake(const mwinAndroidPlatform* platform, bool awake)
{
    if (platform->activity != nullptr)
    {
        ANativeActivity_setWindowFlags(platform->activity, awake ? AWINDOW_FLAG_KEEP_SCREEN_ON : 0,
                                       awake ? 0 : AWINDOW_FLAG_KEEP_SCREEN_ON);
    }
}
