// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// File dialogs on Android (android.h), through the library's Java helper
// maul.window.Documents and the system's document picker.
//
// - Android's documents are content addresses, never paths, and a
//   document's descriptor cannot be opened again by its /proc/self/fd
//   path (it points into shared storage, which the application may not
//   open): each document chosen is copied into the application's cache,
//   a folder of its own per dialog and per document under the name its
//   provider gives (or "Document"), and the dialog answers with the
//   copies' paths, as iOS's picker copies its documents in
//   (android_copy.h): frames go on, and the request completes once every
//   copy is whole. The copies of earlier runs go at the start; the
//   system may empty the cache in time.
// - Only opening is offered: a document the picker creates, or a
//   folder, has no path a program could write through, so saving and
//   choosing a folder are unsupported. A dialog asked for while another
//   waits supersedes it (the core answers the old one): the old picker
//   goes, any copy of its documents stops, and an answer under its
//   number is dropped.
// - Filters offer the content types Android knows of their extensions,
//   all at once; the picker shows no title and starts where it chooses.

#include "android.h"
#include "dialog.h"

#include <stdio.h>
#include <string.h>

static void JNICALL Documents(JNIEnv* env, jclass type, jlong program, jint number, jboolean chosen,
                              jintArray descriptors, jobjectArray names);

bool mwinAndroidFindDialogs(mwinAndroidPlatform* platform, ANativeActivity* activity)
{
    static const JNINativeMethod methods[] = {
        {"nativeDocuments", "(JIZ[I[Ljava/lang/String;)V", (void*)Documents},
    };
    JNIEnv* env = activity->env;
    mwinAndroidDocuments* documents = &platform->documents;
    jclass type = mwinAndroidLoadClass(env, activity, "maul.window.Documents");
    bool found = type != nullptr && (*env)->RegisterNatives(env, type, methods, 1) == JNI_OK;
    if (found)
    {
        documents->type = (*env)->NewGlobalRef(env, type);
        documents->open = (*env)->GetStaticMethodID(
            env, type, "open", "(Landroid/app/Activity;ZLjava/lang/String;II)Z");
        found = documents->type != nullptr && documents->open != nullptr;
    }
    (*env)->ExceptionClear(env);
    (*env)->DeleteLocalRef(env, type);
    return found && mwinAndroidCopyFolder(&documents->copy, env, activity, "maul-documents");
}

void mwinAndroidStopDialogs(mwinAndroidPlatform* platform)
{
    mwinAndroidDocuments* documents = &platform->documents;
    mwinAndroidEndCopy(&platform->context->allocator, &documents->copy);
    if (documents->type != nullptr)
    {
        (*platform->java.env)->DeleteGlobalRef(platform->java.env, documents->type);
        documents->type = nullptr;
    }
}

int mwinAndroidAskDialog(mwinAndroidPlatform* platform, uint32_t slot, uint32_t request)
{
    mwinContext* context = platform->context;
    mwinAndroidDocuments* documents = &platform->documents;
    const mwinDialogCopy* copy = context->windows[slot].requests[request].value.dialog;
    if (copy->kind == mwin_dialogSave || copy->kind == mwin_dialogFolder)
    {
        return mwin_outcomeUnsupported;
    }
    if (platform->activity == nullptr)
    {
        return mwin_outcomeFailed;
    }
    // The dialog before was superseded: its copy stops.
    mwinAndroidEndCopy(&context->allocator, &documents->copy);
    documents->to.waiting = false;
    uint32_t before = documents->number & 0xFFu;
    documents->number += 1;
    // Every filter's extensions, ';' between them.
    char extensions[MWIN_DIALOG_FILTERS * (MWIN_DIALOG_FILTER_BYTES + 1) + 1] = {0};
    size_t length = 0;
    for (uint32_t i = 0; i < copy->filterCount; i++)
    {
        int added = snprintf(extensions + length, sizeof(extensions) - length, "%s%s",
                             length > 0 ? ";" : "", copy->filters[i].extensions);
        length += added > 0 ? (size_t)added : 0;
        length = length < sizeof(extensions) ? length : sizeof(extensions) - 1;
    }
    JNIEnv* env = platform->java.env;
    jstring text = (*env)->NewStringUTF(env, extensions);
    jboolean shown =
        text != nullptr && (*env)->CallStaticBooleanMethod(
                               env, documents->type, documents->open, platform->activity->clazz,
                               (jboolean)(copy->kind == mwin_dialogOpenMany), text,
                               (jint)(documents->number & 0xFFu), (jint)before);
    bool thrown = (*env)->ExceptionCheck(env);
    (*env)->ExceptionClear(env);
    (*env)->DeleteLocalRef(env, text);
    if (!shown || thrown)
    {
        return mwin_outcomeFailed;
    }
    documents->to = mwinAnswerTo(context, slot, request);
    return -1;
}

// Answers the dialog with how it went: done with its copies' paths.
static void Finish(mwinAndroidPlatform* platform, mwinOutcome outcome)
{
    mwinContext* context = platform->context;
    mwinAndroidDocuments* documents = &platform->documents;
    if (mwinAnswerRequest(context, &documents->to) != nullptr)
    {
        mwinBeginDialog(context);
        const mwinAndroidCopy* copy = &documents->copy;
        for (size_t at = 0; outcome == mwin_outcomeDone && at < copy->pathsLength;)
        {
            size_t length = strlen(copy->paths + at);
            mwinAddDialogFile(context, copy->paths + at, length);
            at += length + 1;
        }
        outcome = mwinSettleDialog(context, documents->to.slot, documents->to.request, outcome);
    }
    mwinAndroidEndCopy(&context->allocator, &documents->copy);
    mwinAnswer(context, &documents->to, outcome);
}

static void JNICALL Documents(JNIEnv* env, jclass type, jlong program, jint number, jboolean chosen,
                              jintArray descriptors, jobjectArray names)
{
    (void)type;
    mwinAndroidPlatform* platform = mwinAndroidProgramOf(program);
    // An answer to a dialog no longer waited for, or under an old number,
    // is dropped.
    bool waited = platform != nullptr && platform->documents.to.waiting &&
                  !platform->documents.copy.copying &&
                  (uint32_t)number == (platform->documents.number & 0xFFu);
    jsize count = descriptors != nullptr ? (*env)->GetArrayLength(env, descriptors) : 0;
    if (!waited || !chosen || count == 0 || (uint32_t)count > platform->context->limits.dialogFiles)
    {
        mwinAndroidCloseAll(env, descriptors);
        if (waited)
        {
            Finish(platform, !chosen      ? mwin_outcomeCancelled
                             : count == 0 ? mwin_outcomeFailed
                                          : mwin_outcomeTooLarge);
        }
        return;
    }
    if (!mwinAndroidStartCopy(platform->context, &platform->documents.copy, env, descriptors,
                              names))
    {
        Finish(platform, mwin_outcomeFailed);
    }
}

void mwinAndroidPumpDialogs(mwinAndroidPlatform* platform, uint32_t* budget)
{
    mwinAndroidDocuments* documents = &platform->documents;
    if (documents->copy.copying)
    {
        int state = mwinAndroidStepCopy(&documents->copy, budget);
        if (state != 0)
        {
            Finish(platform, state > 0 ? mwin_outcomeDone : mwin_outcomeFailed);
        }
    }
}
