// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// File dialogs by zenity, where there is no desktop portal: run
// without a shell, its output read through a pipe without blocking at
// each pump. It ends with 0 for a choice, one path a line (or split by
// a separator no path holds, for several), and 1 when closed.

#ifndef MAUL_WINDOW_SRC_LINUX_ZENITY_H
#define MAUL_WINDOW_SRC_LINUX_ZENITY_H

#include "dialog.h"

#include <sys/types.h>

typedef struct mwinZenity
{
    pid_t pid;
    int fd;
    // What it wrote, in a block of capacity bytes from the allocator.
    char* output;
    uint32_t length;
    uint32_t capacity;
    // It wrote more than a choice within the limits could be.
    bool full;
    // Its output ended; it may still be ending.
    bool ended;
} mwinZenity;

// A pattern that matches an extension in any case, *.[pP][nN][gG], into
// out, which holds five bytes an extension byte and two more: its
// length.
size_t mwinCaseBlindPattern(const char* extension, size_t length, char* out);

// Starts zenity for a dialog: -1 while it runs, or the outcome when it
// could not start (unsupported without zenity).
int mwinZenityStart(mwinZenity* zenity, const mwinContext* context, const mwinDialogCopy* copy);

// The paths zenity wrote, split by its separator with the last line's
// end left out, as the dialog's files.
void mwinZenityGather(mwinContext* context, const char* output, size_t length);

// Reads what it wrote: -1 while it runs; else how it ended, the paths
// it chose gathered for the dialog when done.
int mwinZenityPump(mwinZenity* zenity, mwinContext* context);

// Ends it, if it runs, and lets its output go.
void mwinZenityStop(mwinZenity* zenity, const mwinContext* context);

#endif // MAUL_WINDOW_SRC_LINUX_ZENITY_H
