// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Documents copied into the application's cache on Android
// (android_copy.h).

#include "android_copy.h"

#include "allocator.h"

#include "maul-unicode/encoding.h"

#include <errno.h>
#include <fcntl.h>
#include <ftw.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

// The bytes read and written at once, and the longest name a copy keeps.
#define BLOCK_BYTES (64u << 10)
#define NAME_BYTES  255u

static int RemoveEntry(const char* path, const struct stat* status, int flag, struct FTW* walk)
{
    (void)status;
    (void)flag;
    (void)walk;
    (void)remove(path);
    return 0;
}

bool mwinAndroidCopyFolder(mwinAndroidCopy* copy, JNIEnv* env, ANativeActivity* activity,
                           const char* name)
{
    // The cache's path, through Context.getCacheDir().
    jclass activities = (*env)->GetObjectClass(env, activity->clazz);
    jobject cache = (*env)->CallObjectMethod(
        env, activity->clazz,
        (*env)->GetMethodID(env, activities, "getCacheDir", "()Ljava/io/File;"));
    jclass files = cache != nullptr ? (*env)->GetObjectClass(env, cache) : nullptr;
    jstring path = files != nullptr
                       ? (*env)->CallObjectMethod(env, cache,
                                                  (*env)->GetMethodID(env, files, "getAbsolutePath",
                                                                      "()Ljava/lang/String;"))
                       : nullptr;
    const char* bytes = path != nullptr ? (*env)->GetStringUTFChars(env, path, nullptr) : nullptr;
    int written =
        bytes != nullptr ? snprintf(copy->folder, sizeof(copy->folder), "%s/%s", bytes, name) : -1;
    if (bytes != nullptr)
    {
        (*env)->ReleaseStringUTFChars(env, path, bytes);
    }
    (*env)->ExceptionClear(env);
    (*env)->DeleteLocalRef(env, activities);
    (*env)->DeleteLocalRef(env, cache);
    (*env)->DeleteLocalRef(env, files);
    (*env)->DeleteLocalRef(env, path);
    bool named = written > 0 && (size_t)written < sizeof(copy->folder);
    if (named)
    {
        (void)nftw(copy->folder, RemoveEntry, 16, FTW_DEPTH | FTW_PHYS);
    }
    return named;
}

void mwinAndroidCloseAll(JNIEnv* env, jintArray descriptors)
{
    jsize count = descriptors != nullptr ? (*env)->GetArrayLength(env, descriptors) : 0;
    jint* values = count > 0 ? (*env)->GetIntArrayElements(env, descriptors, nullptr) : nullptr;
    for (jsize i = 0; values != nullptr && i < count; i++)
    {
        if (values[i] >= 0)
        {
            (void)close(values[i]);
        }
    }
    if (values != nullptr)
    {
        (*env)->ReleaseIntArrayElements(env, descriptors, values, JNI_ABORT);
    }
}

// A copy's name, ended by a NUL: the provider's in UTF-8, cut to whole
// characters within NAME_BYTES, '/' made '_'; "Document" for none or a
// name that is not one ("." and "..").
static void NameOf(JNIEnv* env, jstring given, char* name)
{
    jsize count = given != nullptr ? (*env)->GetStringLength(env, given) : 0;
    const jchar* units = count > 0 ? (*env)->GetStringChars(env, given, nullptr) : nullptr;
    size_t taken = units != nullptr ? (size_t)count : 0;
    size_t needed = 0;
    // Units dropped from the end until the rest fits, never half a pair.
    for (;;)
    {
        (void)muniConvertUtf16ToUtf8(units, taken, nullptr, 0, muni_convertReplace, &needed);
        if (needed <= NAME_BYTES || taken == 0)
        {
            break;
        }
        taken -= taken >= 2 && units[taken - 1] >= 0xDC00 && units[taken - 1] <= 0xDFFF ? 2 : 1;
    }
    (void)muniConvertUtf16ToUtf8(units, taken, name, NAME_BYTES, muni_convertReplace, &needed);
    name[needed] = '\0';
    if (units != nullptr)
    {
        (*env)->ReleaseStringChars(env, given, units);
    }
    if (needed == 0 || strcmp(name, ".") == 0 || strcmp(name, "..") == 0)
    {
        (void)snprintf(name, NAME_BYTES + 1, "Document");
    }
    for (char* at = strchr(name, '/'); at != nullptr; at = strchr(at, '/'))
    {
        *at = '_';
    }
}

// Keeps a copy's path after the others: false when there is no room.
static bool KeepPath(const mwinAllocator* allocator, mwinAndroidCopy* copy, const char* path,
                     size_t length)
{
    if (copy->pathsLength + length + 1 > copy->pathsCapacity)
    {
        size_t capacity = copy->pathsCapacity * 2 + length + 1;
        char* paths = mwinAllocate(allocator, capacity, 1);
        if (paths == nullptr)
        {
            return false;
        }
        if (copy->paths != nullptr)
        {
            memcpy(paths, copy->paths, copy->pathsLength);
            mwinRelease(allocator, copy->paths, copy->pathsCapacity, 1);
        }
        copy->paths = paths;
        copy->pathsCapacity = capacity;
    }
    memcpy(copy->paths + copy->pathsLength, path, length + 1);
    copy->pathsLength += length + 1;
    return true;
}

// Makes a document's copy: its folder, its file opened to write, its
// path kept. False when it could not be made.
static bool MakeCopy(const mwinAllocator* allocator, mwinAndroidCopy* copy, JNIEnv* env,
                     jstring given, uint32_t index)
{
    char name[NAME_BYTES + 1];
    NameOf(env, given, name);
    // The kind's folder, the job's, and the document's.
    char folder[PATH_MAX];
    char path[PATH_MAX];
    (void)mkdir(copy->folder, 0700);
    (void)snprintf(folder, sizeof(folder), "%s/%u", copy->folder, copy->serial);
    (void)mkdir(folder, 0700);
    int length = snprintf(folder, sizeof(folder), "%s/%u/%u", copy->folder, copy->serial, index);
    if (length <= 0 || (size_t)length >= sizeof(folder) || mkdir(folder, 0700) != 0)
    {
        return false;
    }
    length = snprintf(path, sizeof(path), "%s/%s", folder, name);
    if (length <= 0 || (size_t)length >= sizeof(path))
    {
        return false;
    }
    copy->into[index] = open(path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
    return copy->into[index] >= 0 && KeepPath(allocator, copy, path, (size_t)length);
}

bool mwinAndroidStartCopy(mwinContext* context, mwinAndroidCopy* copy, JNIEnv* env,
                          jintArray descriptors, jobjectArray names)
{
    const mwinAllocator* allocator = &context->allocator;
    uint32_t count = (uint32_t)(*env)->GetArrayLength(env, descriptors);
    int* from = count > 0 ? mwinAllocate(allocator, count * sizeof(int), alignof(int)) : nullptr;
    int* into = count > 0 ? mwinAllocate(allocator, count * sizeof(int), alignof(int)) : nullptr;
    if (from == nullptr || into == nullptr)
    {
        // Nothing taken: Java's descriptors are closed here.
        if (from != nullptr)
        {
            mwinRelease(allocator, from, count * sizeof(int), alignof(int));
        }
        if (into != nullptr)
        {
            mwinRelease(allocator, into, count * sizeof(int), alignof(int));
        }
        mwinAndroidCloseAll(env, descriptors);
        return false;
    }
    (*env)->GetIntArrayRegion(env, descriptors, 0, (jsize)count, from);
    for (uint32_t i = 0; i < count; i++)
    {
        into[i] = -1;
    }
    copy->from = from;
    copy->into = into;
    copy->count = count;
    copy->next = 0;
    copy->pathsLength = 0;
    copy->copying = true;
    copy->serial += 1;
    bool made = true;
    for (uint32_t i = 0; made && i < count; i++)
    {
        jstring name = (*env)->GetObjectArrayElement(env, names, (jsize)i);
        // The copy never waits on a provider's pipe.
        made = from[i] >= 0 && MakeCopy(allocator, copy, env, name, i) &&
               fcntl(from[i], F_SETFL, fcntl(from[i], F_GETFL) | O_NONBLOCK) == 0;
        (*env)->DeleteLocalRef(env, name);
    }
    return made;
}

int mwinAndroidStepCopy(mwinAndroidCopy* copy, uint32_t* budget)
{
    uint8_t block[BLOCK_BYTES];
    while (copy->next < copy->count && *budget > 0)
    {
        uint32_t i = copy->next;
        ssize_t got = read(copy->from[i], block, sizeof(block));
        if (got < 0 && (errno == EAGAIN || errno == EINTR))
        {
            return 0;
        }
        if (got < 0 || (got > 0 && write(copy->into[i], block, (size_t)got) != got))
        {
            return -1;
        }
        if (got == 0)
        {
            // This document is whole.
            (void)close(copy->from[i]);
            bool closed = close(copy->into[i]) == 0;
            copy->next += 1;
            if (!closed)
            {
                return -1;
            }
        }
        *budget -= (uint32_t)got < *budget ? (uint32_t)got : *budget;
    }
    return copy->next == copy->count ? 1 : 0;
}

void mwinAndroidEndCopy(const mwinAllocator* allocator, mwinAndroidCopy* copy)
{
    for (uint32_t i = copy->next; i < copy->count; i++)
    {
        if (copy->from != nullptr && copy->from[i] >= 0)
        {
            (void)close(copy->from[i]);
        }
        if (copy->into != nullptr && copy->into[i] >= 0)
        {
            (void)close(copy->into[i]);
        }
    }
    if (copy->from != nullptr)
    {
        mwinRelease(allocator, copy->from, copy->count * sizeof(int), alignof(int));
    }
    if (copy->into != nullptr)
    {
        mwinRelease(allocator, copy->into, copy->count * sizeof(int), alignof(int));
    }
    if (copy->paths != nullptr)
    {
        mwinRelease(allocator, copy->paths, copy->pathsCapacity, 1);
    }
    copy->from = nullptr;
    copy->into = nullptr;
    copy->paths = nullptr;
    copy->count = 0;
    copy->next = 0;
    copy->pathsLength = 0;
    copy->pathsCapacity = 0;
    copy->copying = false;
}
