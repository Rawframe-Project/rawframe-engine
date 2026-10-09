// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The mouse pointer's icon over the activity's view: a shape as the
// system's PointerIcon of a type, a cursor made from images (mwin-0027)
// as a PointerIcon made once per image from a bitmap. Android draws a
// bitmap's pixels as they are, so the image is the one for the
// display's density, with its hotspot scaled.

#include "android.h"
#include "cursor.h"
#include "icon.h"

// PointerIcon's type of each shape; Android has no progress icon.
static const jint s_types[] = {
    1000, // TYPE_ARROW
    1008, // TYPE_TEXT
    1002, // TYPE_HAND
    1007, // TYPE_CROSSHAIR
    1013, // TYPE_ALL_SCROLL
    1014, // TYPE_HORIZONTAL_DOUBLE_ARROW
    1015, // TYPE_VERTICAL_DOUBLE_ARROW
    1016, // TYPE_TOP_RIGHT_DIAGONAL_DOUBLE_ARROW
    1017, // TYPE_TOP_LEFT_DIAGONAL_DOUBLE_ARROW
    1012, // TYPE_NO_DROP
    1004, // TYPE_WAIT
    1004, // TYPE_WAIT
};

bool mwinAndroidFindCursors(mwinAndroidPlatform* platform)
{
    mwinAndroidJava* java = &platform->java;
    JNIEnv* env = java->env;
    java->showPointerShape =
        (*env)->GetMethodID(env, java->activityClass, "showPointerShape", "(I)V");
    java->showPointer = (*env)->GetMethodID(env, java->activityClass, "showPointer",
                                            "(Landroid/view/PointerIcon;)V");
    java->makePointer = (*env)->GetMethodID(env, java->activityClass, "makePointer",
                                            "([IIIFF)Landroid/view/PointerIcon;");
    (*env)->ExceptionClear(env);
    return java->showPointerShape != nullptr && java->showPointer != nullptr &&
           java->makePointer != nullptr;
}

// The PointerIcon of a cursor's image, a global reference made the
// first time; null when it cannot be made.
static jobject IconOf(mwinAndroidPlatform* platform, mwinCursor* cursor, uint32_t image)
{
    if (cursor->native[image] != nullptr)
    {
        return cursor->native[image];
    }
    JNIEnv* env = platform->java.env;
    const mwinIconCopyImage* source = &cursor->images->images[image];
    jsize count = (jsize)(source->width * source->height);
    jintArray argb = (*env)->NewIntArray(env, count);
    jint* pixels = argb != nullptr ? (*env)->GetIntArrayElements(env, argb, nullptr) : nullptr;
    if (pixels == nullptr)
    {
        (*env)->ExceptionClear(env);
        (*env)->DeleteLocalRef(env, argb);
        return nullptr;
    }
    for (jsize i = 0; i < count; i++)
    {
        const uint8_t* rgba = &source->pixels[(size_t)i * 4];
        pixels[i] = (jint)((uint32_t)rgba[3] << 24 | (uint32_t)rgba[0] << 16 |
                           (uint32_t)rgba[1] << 8 | rgba[2]);
    }
    (*env)->ReleaseIntArrayElements(env, argb, pixels, 0);
    uint32_t x = 0;
    uint32_t y = 0;
    mwinCursorHotspotOf(cursor, image, &x, &y);
    // As jvalues: through C's variable arguments a float would be a double.
    jvalue arguments[] = {{.l = argb},
                          {.i = (jint)source->width},
                          {.i = (jint)source->height},
                          {.f = (jfloat)x},
                          {.f = (jfloat)y}};
    jobject icon = (*env)->CallObjectMethodA(env, platform->activity->clazz,
                                             platform->java.makePointer, arguments);
    (*env)->ExceptionClear(env);
    (*env)->DeleteLocalRef(env, argb);
    cursor->native[image] = icon != nullptr ? (*env)->NewGlobalRef(env, icon) : nullptr;
    (*env)->DeleteLocalRef(env, icon);
    return cursor->native[image];
}

void mwinAndroidApplyCursor(mwinAndroidPlatform* platform)
{
    if (platform->activity == nullptr)
    {
        return;
    }
    JNIEnv* env = platform->java.env;
    const mwinAndroidWindow* window = &platform->window;
    mwinCursor* cursor = mwinFindCursor(platform->context, window->cursorImage);
    jobject icon = cursor != nullptr
                       ? IconOf(platform, cursor, mwinCursorImageFor(cursor, window->scale))
                       : nullptr;
    if (icon != nullptr)
    {
        (*env)->CallVoidMethod(env, platform->activity->clazz, platform->java.showPointer, icon);
    }
    else
    {
        (*env)->CallVoidMethod(env, platform->activity->clazz, platform->java.showPointerShape,
                               s_types[window->cursorShape]);
    }
    (*env)->ExceptionClear(env);
}

mwinOutcome mwinAndroidSetCursorShape(mwinAndroidPlatform* platform, mwinCursorShape shape)
{
    platform->window.cursorShape = shape;
    platform->window.cursorImage = (mwinCursorId){0};
    mwinAndroidApplyCursor(platform);
    return mwin_outcomeDone;
}

mwinOutcome mwinAndroidSetCursorImage(mwinAndroidPlatform* platform, mwinCursorId cursor)
{
    platform->window.cursorImage = cursor;
    mwinAndroidApplyCursor(platform);
    return mwin_outcomeDone;
}

void mwinAndroidReleaseCursor(mwinContext* context, uint32_t slot)
{
    mwinAndroidPlatform* platform = mwinAndroidPlatformOf(context);
    // The window showing it shows the default shape from here.
    if (platform->window.cursorImage.index1 == slot + 1)
    {
        platform->window.cursorShape = mwin_shapeDefault;
        platform->window.cursorImage = (mwinCursorId){0};
        mwinAndroidApplyCursor(platform);
    }
    JNIEnv* env = platform->java.env;
    mwinCursor* cursor = &context->cursors[slot];
    for (uint32_t i = 0; i < MWIN_CURSOR_IMAGES; i++)
    {
        if (cursor->native[i] != nullptr)
        {
            (*env)->DeleteGlobalRef(env, cursor->native[i]);
            cursor->native[i] = nullptr;
        }
    }
}
