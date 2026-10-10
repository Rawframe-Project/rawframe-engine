// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Input methods, the on-screen keyboard and the window's insets on
// Android (android.h), through the library's Java activity, whose
// native methods the backend registers at its start.
//
// - The activity's view is what input methods type into, whatever the
//   program asked: committed text comes as text, a newline and a tab as
//   the Enter and Tab keys' press and release, as on iOS; a composition
//   is told while the window accepts text, and dropped (the input method
//   started again) when it stops. A deletion outside a composition and
//   the keys an input method sends come as keys.
// - The on-screen keyboard shows while the program asks for it, its
//   purpose setting the input type; nothing is corrected, capitalized or
//   completed. The caret's place is not told to the input method.
// - The window is drawn behind the system's bars: its safe area is the
//   bars' and cutouts' insets, and the part the keyboard covers its
//   input method inset at the bottom, both in logical units, told as
//   they change and when the window is made.

#include "allocator.h"
#include "android.h"

#include "maul-unicode/encoding.h"

// The running program, while it has a window; else null.
static mwinAndroidPlatform* Accepting(jlong program)
{
    mwinAndroidPlatform* platform = mwinAndroidProgramOf(program);
    return platform != nullptr && platform->slot >= 0 && platform->window.created ? platform
                                                                                  : nullptr;
}

static void PostEvent(mwinAndroidPlatform* platform, mwinEvent* event)
{
    event->timeNs = mwinAndroidNow();
    mwinPost(platform->context, (uint32_t)platform->slot, event);
}

char* mwinAndroidBytesOf(const mwinAndroidPlatform* platform, JNIEnv* env, jstring text,
                         size_t* sizeOut, int32_t* offsets, size_t offsetCount)
{
    const mwinAllocator* allocator = &platform->context->allocator;
    jsize count = (*env)->GetStringLength(env, text);
    const jchar* units = count > 0 ? (*env)->GetStringChars(env, text, nullptr) : nullptr;
    if (units == nullptr)
    {
        return nullptr;
    }
    size_t size = 0;
    (void)muniConvertUtf16ToUtf8(units, (size_t)count, muni_convertReplace, nullptr, 0, &size);
    char* bytes = size > 0 && size <= UINT32_MAX ? mwinAllocate(allocator, size, 1) : nullptr;
    if (bytes != nullptr)
    {
        (void)muniConvertUtf16ToUtf8(units, (size_t)count, muni_convertReplace, bytes, size,
                                     sizeOut);
        for (size_t i = 0; i < offsetCount; i++)
        {
            size_t before = 0;
            size_t taken = offsets[i] < 0       ? 0
                           : offsets[i] > count ? (size_t)count
                                                : (size_t)offsets[i];
            (void)muniConvertUtf16ToUtf8(units, taken, muni_convertReplace, nullptr, 0, &before);
            offsets[i] = (int32_t)before;
        }
    }
    (*env)->ReleaseStringChars(env, text, units);
    return bytes;
}

static void Tap(mwinAndroidPlatform* platform, int32_t keyCode)
{
    mwinAndroidKey key = {.keyCode = keyCode, .device = -1, .timeNs = mwinAndroidNow()};
    key.action = AKEY_EVENT_ACTION_DOWN;
    (void)mwinAndroidKeyEvent(platform, &key);
    key.action = AKEY_EVENT_ACTION_UP;
    (void)mwinAndroidKeyEvent(platform, &key);
}

static void PostText(mwinAndroidPlatform* platform, const char* bytes, size_t length)
{
    if (length > 0)
    {
        mwinEvent event = {.type = mwin_eventTextInput};
        event.data.text = (mwinTextEvent){bytes, (uint32_t)length};
        PostEvent(platform, &event);
    }
}

static void JNICALL Commit(JNIEnv* env, jclass type, jlong program, jstring text)
{
    (void)type;
    mwinAndroidPlatform* platform = Accepting(program);
    size_t size = 0;
    char* bytes =
        platform != nullptr ? mwinAndroidBytesOf(platform, env, text, &size, nullptr, 0) : nullptr;
    if (bytes == nullptr)
    {
        return;
    }
    size_t start = 0;
    for (size_t i = 0; i < size; i++)
    {
        if (bytes[i] == '\n' || bytes[i] == '\r' || bytes[i] == '\t')
        {
            PostText(platform, bytes + start, i - start);
            Tap(platform, bytes[i] == '\t' ? AKEYCODE_TAB : AKEYCODE_ENTER);
            start = i + 1;
        }
    }
    PostText(platform, bytes + start, size - start);
    mwinRelease(&platform->context->allocator, bytes, size, 1);
}

static void EndPreedit(mwinAndroidPlatform* platform)
{
    if (!platform->context->windows[platform->slot].state.composing)
    {
        return;
    }
    mwinEvent end = {.type = mwin_eventImePreedit};
    end.data.preedit.caret = -1;
    PostEvent(platform, &end);
}

static void JNICALL Compose(JNIEnv* env, jclass type, jlong program, jstring text, jint start,
                            jint end)
{
    (void)type;
    mwinAndroidPlatform* platform = Accepting(program);
    if (platform == nullptr || !platform->window.textInput)
    {
        return;
    }
    int32_t offsets[2] = {start, end};
    size_t size = 0;
    char* bytes = mwinAndroidBytesOf(platform, env, text, &size, offsets, 2);
    if (bytes == nullptr)
    {
        EndPreedit(platform);
        return;
    }
    mwinPreeditSegment segment = {0, (uint32_t)size, mwin_preeditUnderline};
    mwinEvent event = {.type = mwin_eventImePreedit};
    event.data.preedit = (mwinPreeditEvent){
        .text = bytes,
        .length = (uint32_t)size,
        .caret = offsets[1],
        .selectionStart = (uint32_t)offsets[0],
        .selectionEnd = (uint32_t)offsets[1],
        .segments = &segment,
        .segmentCount = 1,
    };
    PostEvent(platform, &event);
    mwinRelease(&platform->context->allocator, bytes, size, 1);
}

static void JNICALL Key(JNIEnv* env, jclass type, jlong program, jint action, jint code, jint meta,
                        jint device)
{
    (void)env;
    (void)type;
    mwinAndroidPlatform* platform = Accepting(program);
    if (platform != nullptr)
    {
        mwinAndroidKey key = {.action = action,
                              .keyCode = code,
                              .meta = meta,
                              .device = device,
                              .timeNs = mwinAndroidNow()};
        (void)mwinAndroidKeyEvent(platform, &key);
    }
}

static bool SameInsets(mwinInsets a, mwinInsets b)
{
    return a.top == b.top && a.right == b.right && a.bottom == b.bottom && a.left == b.left;
}

static bool SameRect(mwinRect a, mwinRect b)
{
    return a.x == b.x && a.y == b.y && a.width == b.width && a.height == b.height;
}

void mwinAndroidPostInsets(mwinAndroidPlatform* platform)
{
    mwinAndroidWindow* window = &platform->window;
    if (platform->slot < 0 || !window->created || window->scale <= 0.0f)
    {
        return;
    }
    const mwinAndroidInsets* insets = &platform->insets;
    float scale = window->scale;
    mwinInsets safe = {(float)insets->top / scale, (float)insets->right / scale,
                       (float)insets->bottom / scale, (float)insets->left / scale};
    mwinRect covered = {0};
    if (insets->keyboard > 0 && (uint32_t)insets->keyboard <= window->height)
    {
        covered = (mwinRect){0.0f, (float)(window->height - (uint32_t)insets->keyboard) / scale,
                             (float)window->width / scale, (float)insets->keyboard / scale};
    }
    if (!SameInsets(safe, window->safeArea))
    {
        window->safeArea = safe;
        mwinEvent event = {.type = mwin_eventSafeAreaChanged};
        event.data.insets = safe;
        PostEvent(platform, &event);
    }
    if (!SameRect(covered, window->covered))
    {
        window->covered = covered;
        mwinEvent event = {.type = mwin_eventVirtualKeyboardChanged};
        event.data.rect = covered;
        PostEvent(platform, &event);
    }
}

static void JNICALL Insets(JNIEnv* env, jclass type, jlong program, jint left, jint top, jint right,
                           jint bottom, jint keyboard)
{
    (void)env;
    (void)type;
    mwinAndroidPlatform* platform = mwinAndroidProgramOf(program);
    if (platform != nullptr)
    {
        platform->insets = (mwinAndroidInsets){left, top, right, bottom, keyboard};
        mwinAndroidPostInsets(platform);
    }
}

bool mwinAndroidFindText(mwinAndroidPlatform* platform, ANativeActivity* activity)
{
    static const JNINativeMethod methods[] = {
        {"nativeCommit", "(JLjava/lang/String;)V", (void*)Commit},
        {"nativeCompose", "(JLjava/lang/String;II)V", (void*)Compose},
        {"nativeKey", "(JIIII)V", (void*)Key},
        {"nativeInsets", "(JIIIII)V", (void*)Insets},
    };
    JNIEnv* env = activity->env;
    jclass library = mwinAndroidLoadClass(env, activity, "maul.window.Activity");
    bool found = library != nullptr &&
                 (*env)->RegisterNatives(env, library, methods,
                                         sizeof(methods) / sizeof(methods[0])) == JNI_OK;
    if (found)
    {
        platform->java.showKeyboard = (*env)->GetMethodID(env, library, "showKeyboard", "(ZI)V");
        platform->java.restartInput = (*env)->GetMethodID(env, library, "restartInput", "()V");
        found = platform->java.showKeyboard != nullptr && platform->java.restartInput != nullptr;
    }
    (*env)->ExceptionClear(env);
    (*env)->DeleteLocalRef(env, library);
    return found;
}

mwinOutcome mwinAndroidSetTextInput(mwinAndroidPlatform* platform, bool enabled, mwinRect caret)
{
    mwinAndroidWindow* window = &platform->window;
    if (!enabled && window->textInput)
    {
        // The composition is dropped, not accepted: the input method
        // starts again with none, and its old connection commits nothing.
        window->textInput = false;
        EndPreedit(platform);
        if (platform->activity != nullptr)
        {
            JNIEnv* env = platform->java.env;
            (*env)->CallVoidMethod(env, platform->activity->clazz, platform->java.restartInput);
            (*env)->ExceptionClear(env);
        }
    }
    window->textInput = enabled;
    window->caret = caret;
    return mwin_outcomeDone;
}

mwinOutcome mwinAndroidSetKeyboard(mwinAndroidPlatform* platform, bool visible,
                                   mwinInputPurpose purpose)
{
    if (platform->activity == nullptr)
    {
        return mwin_outcomeFailed;
    }
    JNIEnv* env = platform->java.env;
    (*env)->CallVoidMethod(env, platform->activity->clazz, platform->java.showKeyboard,
                           (jboolean)visible, (jint)purpose);
    bool thrown = (*env)->ExceptionCheck(env);
    (*env)->ExceptionClear(env);
    return thrown ? mwin_outcomeFailed : mwin_outcomeDone;
}
