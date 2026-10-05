// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Accessibility on Android (android.h), through the library's Java
// helper maul.window.Accessibility.
//
// - The root is the program's android.view.accessibility.
//   AccessibilityNodeProvider, which the library's view gives as its own
//   provider, held by a global reference while it is the root and given
//   by each activity's view in turn; the view's first asking tells the
//   program a client asked. Setting it tells clients to read the tree
//   again.
// - The view is in the window's native handles, a global reference for
//   as long as its activity shows the program: the provider's nodes name
//   it as their source and parent.
// - Touch exploration's hovers, which Android sends with a finger as
//   their tool (a finger cannot hover otherwise), reach the activity's
//   native input rather than its view: while there is a root they are
//   its, and the virtual view under the finger, which the root names
//   through maul.window.Explorer, entered and the one before left, as
//   ExploreByTouchHelper does from a view.

#include "accessibility.h"
#include "android.h"

// View.NO_ID: no virtual view hovered.
#define NO_VIEW (-1)

static jobject JNICALL Root(JNIEnv* env, jclass type, jlong program)
{
    (void)env;
    (void)type;
    mwinAndroidPlatform* platform = mwinAndroidProgramOf(program);
    if (platform == nullptr || platform->slot < 0)
    {
        return nullptr;
    }
    mwinNoteAccessibilityAsked(platform->context, (uint32_t)platform->slot);
    return platform->accessibility.root;
}

bool mwinAndroidFindAccessibility(mwinAndroidPlatform* platform, ANativeActivity* activity)
{
    static const JNINativeMethod methods[] = {
        {"nativeRoot", "(J)Ljava/lang/Object;", (void*)Root},
    };
    JNIEnv* env = activity->env;
    mwinAndroidAccessibility* accessibility = &platform->accessibility;
    accessibility->hovered = NO_VIEW;
    jclass type = mwinAndroidLoadClass(env, activity, "maul.window.Accessibility");
    bool found = type != nullptr && (*env)->RegisterNatives(env, type, methods, 1) == JNI_OK;
    if (found)
    {
        accessibility->type = (*env)->NewGlobalRef(env, type);
        accessibility->viewOf = (*env)->GetStaticMethodID(
            env, type, "viewOf", "(Landroid/app/Activity;)Landroid/view/View;");
        accessibility->changed =
            (*env)->GetStaticMethodID(env, type, "changed", "(Landroid/view/View;)V");
        accessibility->explore = (*env)->GetStaticMethodID(
            env, type, "explore", "(Landroid/view/View;Ljava/lang/Object;IFFI)I");
        found = accessibility->type != nullptr && accessibility->viewOf != nullptr &&
                accessibility->changed != nullptr && accessibility->explore != nullptr;
    }
    (*env)->ExceptionClear(env);
    (*env)->DeleteLocalRef(env, type);
    return found;
}

static void DropView(mwinAndroidPlatform* platform)
{
    mwinAndroidAccessibility* accessibility = &platform->accessibility;
    if (accessibility->view != nullptr)
    {
        (*platform->java.env)->DeleteGlobalRef(platform->java.env, accessibility->view);
        accessibility->view = nullptr;
    }
    accessibility->hovered = NO_VIEW;
}

static void DropRoot(mwinAndroidPlatform* platform)
{
    mwinAndroidAccessibility* accessibility = &platform->accessibility;
    if (accessibility->root != nullptr)
    {
        (*platform->java.env)->DeleteGlobalRef(platform->java.env, accessibility->root);
        accessibility->root = nullptr;
    }
    accessibility->hovered = NO_VIEW;
}

void mwinAndroidStopAccessibility(mwinAndroidPlatform* platform)
{
    DropView(platform);
    DropRoot(platform);
    if (platform->accessibility.type != nullptr)
    {
        (*platform->java.env)->DeleteGlobalRef(platform->java.env, platform->accessibility.type);
        platform->accessibility.type = nullptr;
    }
}

void mwinAndroidJoinAccessibility(mwinAndroidPlatform* platform)
{
    JNIEnv* env = platform->java.env;
    mwinAndroidAccessibility* accessibility = &platform->accessibility;
    if (accessibility->view != nullptr)
    {
        return;
    }
    jobject view = (*env)->CallStaticObjectMethod(env, accessibility->type, accessibility->viewOf,
                                                  platform->activity->clazz);
    (*env)->ExceptionClear(env);
    accessibility->view = view != nullptr ? (*env)->NewGlobalRef(env, view) : nullptr;
    (*env)->DeleteLocalRef(env, view);
}

void mwinAndroidLeaveAccessibility(mwinAndroidPlatform* platform)
{
    DropView(platform);
}

void mwinAndroidForgetAccessibility(mwinAndroidPlatform* platform)
{
    DropRoot(platform);
}

mwinOutcome mwinAndroidSetAccessibilityRoot(mwinAndroidPlatform* platform, void* root)
{
    JNIEnv* env = platform->java.env;
    mwinAndroidAccessibility* accessibility = &platform->accessibility;
    jobject held = root != nullptr ? (*env)->NewGlobalRef(env, (jobject)root) : nullptr;
    if (root != nullptr && held == nullptr)
    {
        (*env)->ExceptionClear(env);
        return mwin_outcomeFailed;
    }
    DropRoot(platform);
    accessibility->root = held;
    if (accessibility->view != nullptr)
    {
        (*env)->CallStaticVoidMethod(env, accessibility->type, accessibility->changed,
                                     accessibility->view);
        (*env)->ExceptionClear(env);
    }
    return mwin_outcomeDone;
}

bool mwinAndroidExplore(mwinAndroidPlatform* platform, const AInputEvent* event)
{
    mwinAndroidAccessibility* accessibility = &platform->accessibility;
    int32_t action = AMotionEvent_getAction(event) & AMOTION_EVENT_ACTION_MASK;
    bool hover = action == AMOTION_EVENT_ACTION_HOVER_ENTER ||
                 action == AMOTION_EVENT_ACTION_HOVER_MOVE ||
                 action == AMOTION_EVENT_ACTION_HOVER_EXIT;
    if (!hover || accessibility->root == nullptr || accessibility->view == nullptr ||
        AMotionEvent_getToolType(event, 0) != AMOTION_EVENT_TOOL_TYPE_FINGER)
    {
        return false;
    }
    JNIEnv* env = platform->java.env;
    const jvalue arguments[] = {
        {.l = accessibility->view},
        {.l = accessibility->root},
        {.i = action},
        {.f = AMotionEvent_getX(event, 0)},
        {.f = AMotionEvent_getY(event, 0)},
        {.i = accessibility->hovered},
    };
    jint hovered =
        (*env)->CallStaticIntMethodA(env, accessibility->type, accessibility->explore, arguments);
    bool thrown = (*env)->ExceptionCheck(env);
    (*env)->ExceptionClear(env);
    accessibility->hovered = thrown ? NO_VIEW : hovered;
    return true;
}
