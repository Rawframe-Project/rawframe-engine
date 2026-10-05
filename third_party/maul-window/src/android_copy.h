// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Documents copied into the application's cache on Android, for file
// dialogs and drops: Android hands documents over as descriptors of
// content addresses, never paths, and a descriptor cannot be opened
// again through its /proc/self/fd path (it points into shared storage).
// A job copies its documents into a folder of its own, one folder per
// document under the name its provider gives (or "Document"), so names
// never meet; the copy runs a step at a time between frames, from
// descriptors that never block, within a budget of bytes the caller
// shares between jobs.

#ifndef MAUL_WINDOW_SRC_ANDROID_COPY_H
#define MAUL_WINDOW_SRC_ANDROID_COPY_H

#include "core.h"

#include <android/native_activity.h>
#include <jni.h>
#include <limits.h>

// The most bytes the copies take a frame.
#define MWIN_ANDROID_COPY_BYTES (8u << 20)

// A kind of job's folder under the cache, and the number of its last
// job (a folder each); while a job copies, its documents' descriptors,
// read from and written to, the next to copy, and the copies' paths,
// each ended by a NUL, in a block from the allocator.
typedef struct mwinAndroidCopy
{
    char folder[PATH_MAX];
    uint32_t serial;
    bool copying;
    int* from;
    int* into;
    uint32_t count;
    uint32_t next;
    char* paths;
    size_t pathsLength;
    size_t pathsCapacity;
} mwinAndroidCopy;

// Names a kind of job's folder under the activity's cache, removing what
// earlier runs left there: false when the cache has no path.
bool mwinAndroidCopyFolder(mwinAndroidCopy* copy, JNIEnv* env, ANativeActivity* activity,
                           const char* name);

// Starts a job: takes the documents' descriptors (Java's, -1 for one
// that could not be opened) and names, and makes the copies' folders and
// files. False when one could not be read or made; the descriptors are
// the job's either way, and mwinAndroidEndCopy closes them.
bool mwinAndroidStartCopy(mwinContext* context, mwinAndroidCopy* copy, JNIEnv* env,
                          jintArray descriptors, jobjectArray names);

// Copies a job a step further, taking from the budget the bytes it
// copied: 1 once every copy is whole, 0 while there is more, -1 when a
// copy failed.
int mwinAndroidStepCopy(mwinAndroidCopy* copy, uint32_t* budget);

// Closes what the job still holds and frees its paths.
void mwinAndroidEndCopy(const mwinAllocator* allocator, mwinAndroidCopy* copy);

// Closes the descriptors Java opened that no job takes.
void mwinAndroidCloseAll(JNIEnv* env, jintArray descriptors);

#endif // MAUL_WINDOW_SRC_ANDROID_COPY_H
