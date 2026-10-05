// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Drag and drop on Android (android.h), through the library's Java
// listener maul.window.Drops on the activity's view, which covers the
// window.
//
// - A drag that carries a type is taken: text for a text type, files for
//   any other (a document is an address of any type). Its place comes in
//   the view's pixels, made logical; entering, moving and leaving are
//   told as they come.
// - A drop's documents are read under the drag's permissions (another
//   application's need them) and copied into the application's cache, a
//   folder per drop (android_copy.h), and its first text item, as text,
//   is its text. The drop is told once every copy is whole, at the place
//   it was made, as on iOS; a document that could not be opened or
//   copied is left out. The window takes no drag while the last drop's
//   documents are still copied.

#include "allocator.h"
#include "android.h"

#include <string.h>

// How Drops.java says a drag moves.
enum
{
    DRAG_ENTERED = 0,
    DRAG_MOVED = 1,
    DRAG_LEFT = 2,
};

// The running program while its window takes drags, else null.
static mwinAndroidPlatform* Taking(jlong program)
{
    mwinAndroidPlatform* platform = mwinAndroidProgramOf(program);
    return platform != nullptr && platform->slot >= 0 && platform->window.created &&
                   platform->window.scale > 0.0f
               ? platform
               : nullptr;
}

static mwinPosition PlaceOf(const mwinAndroidPlatform* platform, jfloat x, jfloat y)
{
    return (mwinPosition){x / platform->window.scale, y / platform->window.scale};
}

static jboolean JNICALL Takes(JNIEnv* env, jclass type, jlong program)
{
    (void)env;
    (void)type;
    mwinAndroidPlatform* platform = Taking(program);
    return platform != nullptr && !platform->drops.copy.copying;
}

static void JNICALL Drag(JNIEnv* env, jclass type, jlong program, jint how, jfloat x, jfloat y,
                         jint contents)
{
    (void)env;
    (void)type;
    mwinAndroidPlatform* platform = Taking(program);
    if (platform == nullptr)
    {
        return;
    }
    mwinEvent event = {.timeNs = mwinAndroidNow()};
    event.type = how == DRAG_ENTERED ? mwin_eventDragEntered
                 : how == DRAG_MOVED ? mwin_eventDragMoved
                                     : mwin_eventDragLeft;
    event.data.drag = (mwinDragEvent){PlaceOf(platform, x, y), (mwinDragContents)contents};
    mwinPost(platform->context, (uint32_t)platform->slot, &event);
}

// Tells the drop with its copies' paths when they are whole, and its text.
static void Finish(mwinAndroidPlatform* platform, bool whole)
{
    mwinContext* context = platform->context;
    mwinAndroidDrops* drops = &platform->drops;
    if (platform->slot >= 0)
    {
        mwinBeginDrop(context);
        const mwinAndroidCopy* copy = &drops->copy;
        for (size_t at = 0; whole && at < copy->pathsLength;)
        {
            size_t length = strlen(copy->paths + at);
            mwinAddDroppedFile(context, copy->paths + at, length);
            at += length + 1;
        }
        if (drops->text != nullptr)
        {
            mwinSetDroppedText(context, drops->text, drops->textLength);
        }
        mwinFinishDrop(context, (uint32_t)platform->slot, drops->place, mwinAndroidNow());
    }
    mwinAndroidEndCopy(&context->allocator, &drops->copy);
    if (drops->text != nullptr)
    {
        mwinRelease(&context->allocator, drops->text, drops->textLength, 1);
        drops->text = nullptr;
    }
}

static void JNICALL Drop(JNIEnv* env, jclass type, jlong program, jfloat x, jfloat y,
                         jintArray descriptors, jobjectArray names, jstring text)
{
    (void)type;
    mwinAndroidPlatform* platform = Taking(program);
    if (platform == nullptr || platform->drops.copy.copying)
    {
        mwinAndroidCloseAll(env, descriptors);
        return;
    }
    mwinAndroidDrops* drops = &platform->drops;
    drops->place = PlaceOf(platform, x, y);
    drops->text = text != nullptr
                      ? mwinAndroidBytesOf(platform, env, text, &drops->textLength, nullptr, 0)
                      : nullptr;
    jsize count = descriptors != nullptr ? (*env)->GetArrayLength(env, descriptors) : 0;
    if (count == 0)
    {
        Finish(platform, true);
        return;
    }
    if (!mwinAndroidStartCopy(platform->context, &drops->copy, env, descriptors, names))
    {
        Finish(platform, false);
    }
}

bool mwinAndroidFindDrops(mwinAndroidPlatform* platform, ANativeActivity* activity)
{
    static const JNINativeMethod methods[] = {
        {"nativeTakes", "(J)Z", (void*)Takes},
        {"nativeDrag", "(JIFFI)V", (void*)Drag},
        {"nativeDrop", "(JFF[I[Ljava/lang/String;Ljava/lang/String;)V", (void*)Drop},
    };
    JNIEnv* env = activity->env;
    jclass type = mwinAndroidLoadClass(env, activity, "maul.window.Drops");
    bool found =
        type != nullptr &&
        (*env)->RegisterNatives(env, type, methods, sizeof(methods) / sizeof(methods[0])) == JNI_OK;
    (*env)->ExceptionClear(env);
    (*env)->DeleteLocalRef(env, type);
    return found && mwinAndroidCopyFolder(&platform->drops.copy, env, activity, "maul-drops");
}

void mwinAndroidStopDrops(mwinAndroidPlatform* platform)
{
    mwinAndroidDrops* drops = &platform->drops;
    mwinAndroidEndCopy(&platform->context->allocator, &drops->copy);
    if (drops->text != nullptr)
    {
        mwinRelease(&platform->context->allocator, drops->text, drops->textLength, 1);
        drops->text = nullptr;
    }
}

void mwinAndroidPumpDrops(mwinAndroidPlatform* platform, uint32_t* budget)
{
    mwinAndroidDrops* drops = &platform->drops;
    if (drops->copy.copying)
    {
        int state = mwinAndroidStepCopy(&drops->copy, budget);
        if (state != 0)
        {
            Finish(platform, state > 0);
        }
    }
}
