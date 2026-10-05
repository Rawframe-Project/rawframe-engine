// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// File dialogs by zenity.

#include "linux_zenity.h"

#include "allocator.h"

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <spawn.h>
#include <stdckdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

extern char** environ;

// What splits the paths of a choice of several: a control character no
// file name holds in practice.
#define SEPARATOR '\x1e'

// The most arguments: the fixed ones and one per filter.
#define ARGUMENTS (8 + MWIN_DIALOG_FILTERS)

// The bytes of a filter argument's start: "--file-filter=" and " |".
#define FILTER_BYTES 32

size_t mwinCaseBlindPattern(const char* extension, size_t length, char* out)
{
    size_t at = 0;
    out[at++] = '*';
    out[at++] = '.';
    for (size_t i = 0; i < length; i++)
    {
        char c = extension[i];
        char lower = c >= 'A' && c <= 'Z' ? (char)(c - 'A' + 'a') : c;
        char upper = c >= 'a' && c <= 'z' ? (char)(c - 'a' + 'A') : c;
        if (lower != upper)
        {
            out[at++] = '[';
            out[at++] = lower;
            out[at++] = upper;
            out[at++] = ']';
        }
        else
        {
            out[at++] = c;
        }
    }
    return at;
}

// zenity's filter: "Images | *.[pP][nN][gG] *.[jJ][pP][gG]".
static char* Filter(char* at, const char* end, const mwinDialogFilter* filter)
{
    at += snprintf(at, (size_t)(end - at), "--file-filter=%s |", filter->name);
    const char* extension = filter->extensions;
    while (*extension != '\0')
    {
        size_t length = strcspn(extension, ";");
        *at++ = ' ';
        at += mwinCaseBlindPattern(extension, length, at);
        extension += length + (extension[length] == ';' ? 1 : 0);
    }
    *at++ = '\0';
    return at;
}

// The bytes the arguments may take: a pattern is at most five bytes an
// extension byte and three more, and a filter's name comes once.
static size_t ArgumentBytes(const mwinDialogCopy* copy)
{
    size_t bytes = 64 + copy->titleLength + copy->folderLength + copy->nameLength;
    for (uint32_t i = 0; i < copy->filterCount; i++)
    {
        bytes +=
            FILTER_BYTES + strlen(copy->filters[i].name) + 8 * strlen(copy->filters[i].extensions);
    }
    return bytes;
}

// Writes an argument of up to four parts at *at, and moves it on.
static char* Argument(char** at, const char* end, const char* const parts[4])
{
    char* start = *at;
    *at +=
        snprintf(start, (size_t)(end - start), "%s%s%s%s", parts[0], parts[1], parts[2], parts[3]) +
        1;
    return start;
}

// zenity's arguments, their text in a block of bytes.
static void Arguments(const mwinDialogCopy* copy, char* text, size_t bytes, char** arguments)
{
    size_t count = 0;
    arguments[count++] = (char*)"zenity";
    arguments[count++] = (char*)"--file-selection";
    char* at = text;
    const char* end = text + bytes;
    if (copy->titleLength > 0)
    {
        arguments[count++] =
            Argument(&at, end, (const char* const[4]){"--title=", copy->title, "", ""});
    }
    if (copy->kind == mwin_dialogOpenMany)
    {
        arguments[count++] = (char*)"--multiple";
        arguments[count++] = (char*)"--separator=\x1e";
    }
    if (copy->kind == mwin_dialogFolder || copy->kind == mwin_dialogSave)
    {
        arguments[count++] =
            copy->kind == mwin_dialogFolder ? (char*)"--directory" : (char*)"--save";
    }
    if (copy->folderLength > 0 || (copy->kind == mwin_dialogSave && copy->nameLength > 0))
    {
        // A folder is named with its last slash; a save's name after it.
        const char* name = copy->kind == mwin_dialogSave ? copy->name : "";
        const char* slash = copy->folderLength > 0 ? "/" : "";
        arguments[count++] =
            Argument(&at, end, (const char* const[4]){"--filename=", copy->folder, slash, name});
    }
    for (uint32_t i = 0; i < copy->filterCount; i++)
    {
        arguments[count++] = at;
        at = Filter(at, end, &copy->filters[i]);
    }
    arguments[count] = nullptr;
}

static int Spawn(mwinZenity* zenity, char** arguments)
{
    int ends[2];
    if (pipe(ends) != 0)
    {
        return mwin_outcomeFailed;
    }
    (void)fcntl(ends[0], F_SETFD, FD_CLOEXEC);
    (void)fcntl(ends[1], F_SETFD, FD_CLOEXEC);
    (void)fcntl(ends[0], F_SETFL, O_NONBLOCK);
    posix_spawn_file_actions_t actions;
    (void)posix_spawn_file_actions_init(&actions);
    (void)posix_spawn_file_actions_adddup2(&actions, ends[1], STDOUT_FILENO);
    int spawned = posix_spawnp(&zenity->pid, arguments[0], &actions, nullptr, arguments, environ);
    (void)posix_spawn_file_actions_destroy(&actions);
    (void)close(ends[1]);
    if (spawned != 0)
    {
        (void)close(ends[0]);
        zenity->pid = 0;
        return spawned == ENOENT ? mwin_outcomeUnsupported : mwin_outcomeFailed;
    }
    zenity->fd = ends[0];
    return -1;
}

int mwinZenityStart(mwinZenity* zenity, const mwinContext* context, const mwinDialogCopy* copy)
{
    *zenity = (mwinZenity){.fd = -1};
    size_t bytes = ArgumentBytes(copy);
    char* text = mwinAllocate(&context->allocator, bytes, 1);
    if (text == nullptr)
    {
        return mwin_outcomeFailed;
    }
    char* arguments[ARGUMENTS];
    Arguments(copy, text, bytes, arguments);
    int started = Spawn(zenity, arguments);
    mwinRelease(&context->allocator, text, bytes, 1);
    return started;
}

// Room for more output, bounded by what a choice within the limits
// could write.
static bool Grow(mwinZenity* zenity, const mwinContext* context)
{
    size_t bound = (size_t)context->limits.dialogBytes + 1;
    if (zenity->length < zenity->capacity)
    {
        return true;
    }
    if (zenity->capacity >= bound)
    {
        zenity->full = true;
        return false;
    }
    // Doubling saturates, then the bound caps it.
    size_t capacity = 4096;
    if (zenity->capacity != 0 && ckd_mul(&capacity, (size_t)zenity->capacity, 2))
    {
        capacity = SIZE_MAX;
    }
    capacity = capacity < bound ? capacity : bound;
    char* grown = mwinAllocate(&context->allocator, capacity, 1);
    if (grown == nullptr)
    {
        zenity->full = true;
        return false;
    }
    if (zenity->output != nullptr)
    {
        memcpy(grown, zenity->output, zenity->length);
        mwinRelease(&context->allocator, zenity->output, zenity->capacity, 1);
    }
    zenity->output = grown;
    zenity->capacity = (uint32_t)capacity;
    return true;
}

static void Read(mwinZenity* zenity, const mwinContext* context)
{
    while (!zenity->ended)
    {
        char spill[256];
        bool room = Grow(zenity, context);
        char* into = room ? zenity->output + zenity->length : spill;
        size_t size = room ? zenity->capacity - zenity->length : sizeof(spill);
        ssize_t got = read(zenity->fd, into, size);
        if (got > 0)
        {
            zenity->length += room ? (uint32_t)got : 0;
        }
        else if (got == 0 || (errno != EAGAIN && errno != EINTR))
        {
            zenity->ended = true;
        }
        else if (errno == EAGAIN)
        {
            return;
        }
    }
}

void mwinZenityGather(mwinContext* context, const char* output, size_t length)
{
    mwinBeginDialog(context);
    while (length > 0 && output[length - 1] == '\n')
    {
        length--;
    }
    size_t start = 0;
    while (start < length)
    {
        const char* end = memchr(output + start, SEPARATOR, length - start);
        size_t size = (end != nullptr ? (size_t)(end - output) : length) - start;
        mwinAddDialogFile(context, output + start, size);
        start += size + 1;
    }
}

int mwinZenityPump(mwinZenity* zenity, mwinContext* context)
{
    Read(zenity, context);
    int status = 0;
    if (!zenity->ended || waitpid(zenity->pid, &status, WNOHANG) != zenity->pid)
    {
        return -1;
    }
    zenity->pid = 0;
    if (!WIFEXITED(status) || WEXITSTATUS(status) > 1)
    {
        return mwin_outcomeFailed;
    }
    if (WEXITSTATUS(status) == 1)
    {
        return mwin_outcomeCancelled;
    }
    if (zenity->full)
    {
        return mwin_outcomeTooLarge;
    }
    mwinZenityGather(context, zenity->output, zenity->length);
    return mwin_outcomeDone;
}

void mwinZenityStop(mwinZenity* zenity, const mwinContext* context)
{
    if (zenity->pid > 0)
    {
        (void)kill(zenity->pid, SIGTERM);
        (void)waitpid(zenity->pid, nullptr, 0);
    }
    if (zenity->fd >= 0)
    {
        (void)close(zenity->fd);
    }
    if (zenity->output != nullptr)
    {
        mwinRelease(&context->allocator, zenity->output, zenity->capacity, 1);
    }
    *zenity = (mwinZenity){.fd = -1};
}
