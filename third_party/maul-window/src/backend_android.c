// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The Android backend (mwin-0026). The platform begins the run: the
// library's activity (maul.window.Activity, a NativeActivity) calls
// ANativeActivity_onCreate, which asks the program for itself
// (mwinAndroidMain) and runs it as mwinRun would, with the activity as
// the backend's launch. Init runs at once; the run then returns, and the
// main thread's looper drives the rest: the activity's callbacks, its
// input queue, and the frames, which the choreographer calls at each
// vsync while the activity is started. The library starts no thread.
//
// The program outlives its activity. The activity's class keeps the
// running program in a static field, so an activity the system creates
// anew (a configuration change, the user coming back) joins it; the
// window's surface goes with the old activity and comes with the new.
// When the activity stops the program is told the application is
// suspending, in a frame of its own, then suspended, and frames pause; a
// start resumes them the same way. A frame that stops the program, here
// or in a critical frame, calls quit, finishes the activity and frees the
// context; a frame callback still waiting then frees it instead, as the
// choreographer cannot take one back.

#include "allocator.h"
#include "android.h"
#include "backend.h"
#include "key_reach.h"

#include <string.h>
#include <time.h>

// The program's entry (context.h), which a program built without it
// lacks: then no activity can run it.
#pragma weak mwinAndroidMain

mwinAndroidPlatform* mwinAndroidPlatformOf(const mwinContext* context)
{
    return (mwinAndroidPlatform*)context->backendData;
}

uint64_t mwinAndroidNow(void)
{
    struct timespec now;
    (void)clock_gettime(CLOCK_MONOTONIC, &now);
    return (uint64_t)now.tv_sec * 1000000000ull + (uint64_t)now.tv_nsec;
}

float mwinAndroidScale(const mwinAndroidPlatform* platform)
{
    int32_t density = AConfiguration_getDensity(platform->configuration);
    if (density == ACONFIGURATION_DENSITY_DEFAULT || density >= ACONFIGURATION_DENSITY_ANY)
    {
        density = ACONFIGURATION_DENSITY_MEDIUM;
    }
    return (float)density / (float)ACONFIGURATION_DENSITY_MEDIUM;
}

// The activity's field that holds the running program, with the class it
// was found on (a local reference); null when the activity is not the
// library's.
static jfieldID ProgramField(JNIEnv* env, jobject activity, jclass* classOut)
{
    jclass found = (*env)->GetObjectClass(env, activity);
    jfieldID field = (*env)->GetStaticFieldID(env, found, "program", "J");
    if (field == nullptr)
    {
        (*env)->ExceptionClear(env);
        (*env)->DeleteLocalRef(env, found);
        return nullptr;
    }
    *classOut = found;
    return field;
}

// The running program as the Java long holds it: Android's ABIs are all
// little-endian, so a 32-bit pointer is the long's low half.
typedef union ProgramLong
{
    jlong value;
    mwinAndroidPlatform* program;
} ProgramLong;

jclass mwinAndroidLoadClass(JNIEnv* env, ANativeActivity* activity, const char* name)
{
    // Through the application's class loader: NativeActivity's own, which
    // FindClass uses on the main thread, cannot see the library's classes.
    jclass type = (*env)->GetObjectClass(env, activity->clazz);
    jmethodID loaderOf =
        (*env)->GetMethodID(env, type, "getClassLoader", "()Ljava/lang/ClassLoader;");
    jobject loader = (*env)->CallObjectMethod(env, activity->clazz, loaderOf);
    jclass loaders = loader != nullptr ? (*env)->GetObjectClass(env, loader) : nullptr;
    jmethodID load = loaders != nullptr
                         ? (*env)->GetMethodID(env, loaders, "loadClass",
                                               "(Ljava/lang/String;)Ljava/lang/Class;")
                         : nullptr;
    jstring text = (*env)->NewStringUTF(env, name);
    jclass found =
        load != nullptr ? (jclass)(*env)->CallObjectMethod(env, loader, load, text) : nullptr;
    if ((*env)->ExceptionCheck(env))
    {
        (*env)->ExceptionClear(env);
        found = nullptr;
    }
    (*env)->DeleteLocalRef(env, type);
    (*env)->DeleteLocalRef(env, loader);
    (*env)->DeleteLocalRef(env, loaders);
    (*env)->DeleteLocalRef(env, text);
    return found;
}

mwinAndroidPlatform* mwinAndroidProgramOf(jlong value)
{
    ProgramLong held = {.value = value};
    return held.program;
}

static void SetProgram(const mwinAndroidPlatform* platform, mwinAndroidPlatform* program)
{
    const mwinAndroidJava* java = &platform->java;
    ProgramLong held = {.value = 0};
    held.program = program;
    (*java->env)->SetStaticLongField(java->env, java->activityClass, java->program, held.value);
}

// Whether the activity is finishing, not merely being made anew.
static bool IsFinishing(const mwinAndroidPlatform* platform, ANativeActivity* activity)
{
    JNIEnv* env = platform->java.env;
    jclass type = (*env)->GetObjectClass(env, activity->clazz);
    jmethodID method = (*env)->GetMethodID(env, type, "isFinishing", "()Z");
    bool finishing = method != nullptr && (*env)->CallBooleanMethod(env, activity->clazz, method);
    (*env)->ExceptionClear(env);
    (*env)->DeleteLocalRef(env, type);
    return finishing;
}

// Lets go of the activity's parts: its input queue, its native window,
// its view, and the activity itself, whose callbacks then find no
// program.
static void Detach(mwinAndroidPlatform* platform)
{
    if (platform->queue != nullptr)
    {
        AInputQueue_detachLooper(platform->queue);
        platform->queue = nullptr;
    }
    if (platform->nativeWindow != nullptr)
    {
        ANativeWindow_release(platform->nativeWindow);
        platform->nativeWindow = nullptr;
    }
    if (platform->activity != nullptr)
    {
        mwinAndroidLeaveAccessibility(platform);
        platform->activity->instance = nullptr;
        platform->activity = nullptr;
    }
    platform->started = false;
}

// Ends the program: quit, the activity finished, and the context freed,
// at once or by the frame callback that waits.
static void Finish(mwinAndroidPlatform* platform)
{
    mwinContext* context = platform->context;
    SetProgram(platform, nullptr);
    (void)mwinEndProgram(context);
    if (platform->activity != nullptr)
    {
        ANativeActivity_finish(platform->activity);
    }
    Detach(platform);
    if (platform->framePosted)
    {
        platform->ended = true;
        return;
    }
    mwinFinishRun(context);
}

// Between frames the dialog's and the drop's documents are copied a step
// further, at most MWIN_ANDROID_COPY_BYTES a frame, the power read and
// the gamepads looked for when due.
static void Pump(mwinContext* context)
{
    mwinAndroidPlatform* platform = mwinAndroidPlatformOf(context);
    uint32_t budget = MWIN_ANDROID_COPY_BYTES;
    mwinAndroidPumpDialogs(platform, &budget);
    mwinAndroidPumpDrops(platform, &budget);
    mwinAndroidPumpFacts(platform, mwinAndroidNow());
    mwinAndroidPumpScreen(platform, mwinAndroidNow());
#ifdef MAUL_WINDOW_GAMEPAD
    mwinAndroidPumpPads(platform, mwinAndroidNow());
#endif
}

static void OnFrame(int64_t frameTimeNanos, void* data);

static void PostFrame(mwinAndroidPlatform* platform)
{
    if (!platform->framePosted && platform->started)
    {
        AChoreographer_postFrameCallback64(platform->choreographer, OnFrame, platform);
        platform->framePosted = true;
    }
}

static void OnFrame(int64_t frameTimeNanos, void* data)
{
    (void)frameTimeNanos;
    mwinAndroidPlatform* platform = data;
    platform->framePosted = false;
    if (platform->ended)
    {
        mwinFinishRun(platform->context);
        return;
    }
    if (!platform->started)
    {
        return;
    }
    if (!mwinStepProgram(platform->context, Pump))
    {
        Finish(platform);
        return;
    }
    PostFrame(platform);
}

static void PostLifecycle(mwinContext* context, mwinEventType type)
{
    mwinEvent event = {.type = type, .timeNs = mwinAndroidNow()};
    mwinPostGlobal(context, &event);
}

// The application stopped or runs again: the program hears of it at
// once, in a frame of its own, which may stop it. False once it stopped.
static bool Lifecycle(mwinAndroidPlatform* platform, bool running)
{
    mwinContext* context = platform->context;
    platform->suspended = !running;
    // Suspending resets the input state: keys and pointers held are
    // forgotten.
    mwinAndroidForgetInput(platform);
    PostLifecycle(context, running ? mwin_eventResuming : mwin_eventSuspending);
    mwinRunCriticalFrame(context);
    if (context->stopping)
    {
        Finish(platform);
        return false;
    }
    PostLifecycle(context, running ? mwin_eventResumed : mwin_eventSuspended);
    return true;
}

static void OnStart(ANativeActivity* activity)
{
    mwinAndroidPlatform* platform = activity->instance;
    if (platform == nullptr)
    {
        return;
    }
    platform->started = true;
    // The activity's view exists from its start: its creation calls the
    // library before making it.
    mwinAndroidJoinAccessibility(platform);
    // Settings changed in the settings application come back with it.
    mwinAndroidReadSystem(platform);
    mwinAndroidReadScreen(platform);
    if (platform->suspended && !Lifecycle(platform, true))
    {
        return;
    }
    PostFrame(platform);
}

static void OnStop(ANativeActivity* activity)
{
    mwinAndroidPlatform* platform = activity->instance;
    if (platform == nullptr)
    {
        return;
    }
    platform->started = false;
    (void)Lifecycle(platform, false);
}

// The program's activity ends. The user or the program finished it: the
// window can no longer show. An activity made anew is not asked about.
static void End(mwinAndroidPlatform* platform, ANativeActivity* activity)
{
    if (platform->slot >= 0 && IsFinishing(platform, activity))
    {
        mwinEvent event = {.type = mwin_eventCloseRequested, .timeNs = mwinAndroidNow()};
        mwinPost(platform->context, (uint32_t)platform->slot, &event);
    }
    Detach(platform);
}

static void OnDestroy(ANativeActivity* activity)
{
    mwinAndroidPlatform* platform = activity->instance;
    if (platform != nullptr)
    {
        End(platform, activity);
    }
}

static void OnWindowFocusChanged(ANativeActivity* activity, int hasFocus)
{
    mwinAndroidPlatform* platform = activity->instance;
    if (platform != nullptr)
    {
        platform->focused = hasFocus != 0;
        mwinAndroidPostFocus(platform);
    }
}

static void OnNativeWindowCreated(ANativeActivity* activity, ANativeWindow* window)
{
    mwinAndroidPlatform* platform = activity->instance;
    if (platform == nullptr)
    {
        return;
    }
    ANativeWindow_acquire(window);
    platform->nativeWindow = window;
    mwinAndroidSurfaceCame(platform);
}

static void OnNativeWindowResized(ANativeActivity* activity, ANativeWindow* window)
{
    (void)window;
    mwinAndroidPlatform* platform = activity->instance;
    if (platform != nullptr)
    {
        mwinAndroidReadSize(platform);
    }
}

// Android waits for this to return before it takes the surface away: the
// program stops drawing to it in a frame of its own.
static void OnNativeWindowDestroyed(ANativeActivity* activity, ANativeWindow* window)
{
    (void)window;
    mwinAndroidPlatform* platform = activity->instance;
    if (platform == nullptr)
    {
        return;
    }
    mwinContext* context = platform->context;
    mwinAndroidSurfaceWent(platform);
    mwinRunCriticalFrame(context);
    ANativeWindow_release(platform->nativeWindow);
    platform->nativeWindow = nullptr;
    if (context->stopping)
    {
        Finish(platform);
    }
}

// While the window accepts text, each event goes to the input method
// first (it composes from a keyboard's keys too); what it leaves comes to
// the window, and what the program does not take goes on to Android.
// Otherwise the input method never sees the keys, which it would take
// some of (Escape, for one).
static int OnInput(int fd, int events, void* data)
{
    (void)fd;
    (void)events;
    mwinAndroidPlatform* platform = data;
    AInputEvent* event = nullptr;
    while (platform->queue != nullptr && AInputQueue_getEvent(platform->queue, &event) >= 0)
    {
        if (platform->window.textInput && AInputQueue_preDispatchEvent(platform->queue, event) != 0)
        {
            continue;
        }
        AInputQueue_finishEvent(platform->queue, event, mwinAndroidInput(platform, event) ? 1 : 0);
    }
    return 1;
}

static void OnInputQueueCreated(ANativeActivity* activity, AInputQueue* queue)
{
    mwinAndroidPlatform* platform = activity->instance;
    if (platform == nullptr)
    {
        return;
    }
    platform->queue = queue;
    AInputQueue_attachLooper(queue, platform->looper, ALOOPER_POLL_CALLBACK, OnInput, platform);
}

static void OnInputQueueDestroyed(ANativeActivity* activity, AInputQueue* queue)
{
    mwinAndroidPlatform* platform = activity->instance;
    if (platform != nullptr && platform->queue == queue)
    {
        AInputQueue_detachLooper(queue);
        platform->queue = nullptr;
    }
}

static void OnConfigurationChanged(ANativeActivity* activity)
{
    mwinAndroidPlatform* platform = activity->instance;
    if (platform != nullptr)
    {
        AConfiguration_fromAssetManager(platform->configuration, activity->assetManager);
        mwinAndroidReadSize(platform);
        mwinAndroidReadSystem(platform);
        mwinAndroidReadScreen(platform);
    }
}

// Android can make an activity before the one it follows ends, as when a
// start comes at once after a finish: the old one ends as the new one
// joins, as its own end would (its surface told gone first), so that its
// later callbacks, its stop among them, find no program. False when the
// program stopped meanwhile and has ended.
static bool EndOld(mwinAndroidPlatform* platform)
{
    mwinContext* context = platform->context;
    if (platform->nativeWindow != nullptr)
    {
        mwinAndroidSurfaceWent(platform);
        mwinRunCriticalFrame(context);
        ANativeWindow_release(platform->nativeWindow);
        platform->nativeWindow = nullptr;
        if (context->stopping)
        {
            Finish(platform);
            return false;
        }
    }
    End(platform, platform->activity);
    return true;
}

// The program shows in the activity from now on.
static void Attach(mwinAndroidPlatform* platform, ANativeActivity* activity)
{
    platform->activity = activity;
    activity->instance = platform;
    ANativeActivityCallbacks* callbacks = activity->callbacks;
    callbacks->onStart = OnStart;
    callbacks->onStop = OnStop;
    callbacks->onDestroy = OnDestroy;
    callbacks->onWindowFocusChanged = OnWindowFocusChanged;
    callbacks->onNativeWindowCreated = OnNativeWindowCreated;
    callbacks->onNativeWindowResized = OnNativeWindowResized;
    callbacks->onNativeWindowDestroyed = OnNativeWindowDestroyed;
    callbacks->onInputQueueCreated = OnInputQueueCreated;
    callbacks->onInputQueueDestroyed = OnInputQueueDestroyed;
    callbacks->onConfigurationChanged = OnConfigurationChanged;
    AConfiguration_fromAssetManager(platform->configuration, activity->assetManager);
    // A window kept awake keeps the joining activity's display awake too,
    // and its cursor shows over the joining activity's view.
    if (platform->slot >= 0 && platform->context->windows[platform->slot].state.awake)
    {
        mwinAndroidApplyAwake(platform, true);
    }
    if (platform->slot >= 0)
    {
        mwinAndroidApplyCursor(platform);
    }
}

__attribute__((visibility("default"))) void
ANativeActivity_onCreate(ANativeActivity* activity, void* savedState, size_t savedStateSize)
{
    (void)savedState;
    (void)savedStateSize;
    JNIEnv* env = activity->env;
    jclass type = nullptr;
    jfieldID field = ProgramField(env, activity->clazz, &type);
    // Only the library's activity can hold the program; one a program
    // built without its entry cannot run.
    if (field == nullptr || mwinAndroidMain == nullptr)
    {
        ANativeActivity_finish(activity);
        return;
    }
    mwinAndroidPlatform* running =
        mwinAndroidProgramOf((*env)->GetStaticLongField(env, type, field));
    (*env)->DeleteLocalRef(env, type);
    if (running != nullptr)
    {
        if (running->activity == nullptr || EndOld(running))
        {
            Attach(running, activity);
        }
        else
        {
            ANativeActivity_finish(activity);
        }
        return;
    }
    mwinAppDef def = mwinAndroidMain();
    if (mwinRunLaunched(&def, activity) != mwin_success)
    {
        ANativeActivity_finish(activity);
    }
}

static void Stop(mwinContext* context)
{
    mwinAndroidPlatform* platform = mwinAndroidPlatformOf(context);
    Detach(platform);
    mwinAndroidLoseInput(platform);
    mwinAndroidStopServices(platform);
    mwinAndroidStopDialogs(platform);
    mwinAndroidStopDrops(platform);
    mwinAndroidStopFacts(platform);
    mwinAndroidStopScreen(platform);
    mwinAndroidStopAccessibility(platform);
#ifdef MAUL_WINDOW_GAMEPAD
    mwinAndroidStopPads(platform);
#endif
    AConfiguration_delete(platform->configuration);
    (*platform->java.env)->DeleteGlobalRef(platform->java.env, platform->java.activityClass);
    mwinRelease(&context->allocator, platform, sizeof(*platform), alignof(mwinAndroidPlatform));
    context->backendData = nullptr;
}

static mwinResult Start(mwinContext* context)
{
    // A program runs through mwinAndroidMain here, never mwinRun.
    ANativeActivity* activity = context->launch;
    if (activity == nullptr)
    {
        return mwin_errorUnsupported;
    }
    JNIEnv* env = activity->env;
    jclass type = nullptr;
    jfieldID field = ProgramField(env, activity->clazz, &type);
    if (field == nullptr)
    {
        return mwin_errorPlatform;
    }
    mwinAndroidPlatform* platform =
        mwinAllocate(&context->allocator, sizeof(*platform), alignof(mwinAndroidPlatform));
    AConfiguration* configuration = platform != nullptr ? AConfiguration_new() : nullptr;
    if (configuration == nullptr)
    {
        mwinRelease(&context->allocator, platform, sizeof(*platform), alignof(mwinAndroidPlatform));
        (*env)->DeleteLocalRef(env, type);
        return mwin_errorCapacity;
    }
    *platform = (mwinAndroidPlatform){
        .context = context,
        .java = {.env = env, .activityClass = (*env)->NewGlobalRef(env, type), .program = field},
        .configuration = configuration,
        .looper = ALooper_forThread(),
        .choreographer = AChoreographer_getInstance(),
        .slot = -1,
    };
    (*env)->DeleteLocalRef(env, type);
    context->backendData = platform;
    bool found =
        mwinAndroidFindInput(platform) && mwinAndroidFindText(platform, activity) &&
        mwinAndroidFindCursors(platform) && mwinAndroidFindServices(platform, activity) &&
        mwinAndroidFindDialogs(platform, activity) && mwinAndroidFindDrops(platform, activity) &&
        mwinAndroidFindFacts(platform, activity) && mwinAndroidFindScreen(platform, activity) &&
        mwinAndroidFindAccessibility(platform, activity);
#ifdef MAUL_WINDOW_GAMEPAD
    found = found && mwinAndroidFindPads(platform, activity);
#endif
    if (!found)
    {
        Stop(context);
        return mwin_errorPlatform;
    }
    Attach(platform, activity);
    return mwin_success;
}

// Init runs now; the looper runs the rest once the run returned.
static mwinResult Run(mwinContext* context)
{
    if (!mwinStartProgram(context))
    {
        return mwinEndProgram(context);
    }
    context->loopOutlivesRun = true;
    mwinAndroidPlatform* platform = mwinAndroidPlatformOf(context);
    SetProgram(platform, platform);
    return mwin_success;
}

static uint64_t Now(const mwinContext* context)
{
    (void)context;
    return mwinAndroidNow();
}

static mwinKey MapKeyCode(const mwinContext* context, mwinKeyCode code)
{
    return mwinAndroidMapKeyCode(mwinAndroidPlatformOf(context), code);
}

static mwinResult KeyboardLayout(const mwinContext* context, char* buffer, size_t capacity,
                                 size_t* lengthOut)
{
    (void)context;
    (void)buffer;
    (void)capacity;
    *lengthOut = 0;
    return mwin_success;
}

static void NativeHandles(const mwinContext* context, uint32_t slot, mwinNativeHandles* out)
{
    (void)slot;
    const mwinAndroidPlatform* platform = mwinAndroidPlatformOf(context);
    out->platform = mwin_platformAndroid;
    out->handles.android.window = platform->nativeWindow;
    out->handles.android.activity = platform->activity;
    out->handles.android.view = platform->accessibility.view;
}

static mwinResult Rumble(mwinContext* context, uint32_t slot, float low, float high,
                         uint32_t durationMs)
{
#ifdef MAUL_WINDOW_GAMEPAD
    return mwinAndroidRumble(mwinAndroidPlatformOf(context), slot, low, high, durationMs);
#else
    (void)context;
    (void)slot;
    (void)low;
    (void)high;
    (void)durationMs;
    return mwin_errorUnsupported;
#endif
}

#ifdef MAUL_WINDOW_GAMEPAD
static mwinResult SetMotion(mwinContext* context, uint32_t slot, bool enabled)
{
    return mwinAndroidSetMotion(mwinAndroidPlatformOf(context), slot, enabled);
}
#endif

const mwinBackendOps mwinAndroidBackend = {
    Start,
    Stop,
    Run,
    mwinAndroidCreateWindow,
    mwinAndroidDestroyWindow,
    mwinAndroidSubmit,
    Now,
    MapKeyCode,
    KeyboardLayout,
    NativeHandles,
    Rumble,
    mwinAndroidReleaseCursor,
    nullptr,
#ifdef MAUL_WINDOW_GAMEPAD
    SetMotion,
#else
    nullptr,
#endif
    mwinAndroidKeyReach,
};
