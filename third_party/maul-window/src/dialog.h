// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// File dialogs for the backends: the copy of a def a request holds,
// and the paths a dialog chose, gathered, then settled as the request's
// answer.

#ifndef MAUL_WINDOW_SRC_DIALOG_H
#define MAUL_WINDOW_SRC_DIALOG_H

#include "core.h"

#include "maul-window/dialog.h"

// A filter's name and extensions, each ended by a NUL.
typedef struct mwinDialogFilter
{
    const char* name;
    const char* extensions;
} mwinDialogFilter;

// A def as a request holds it: one block from the allocator, of size
// bytes, its text after the filters, each string ended by a NUL.
typedef struct mwinDialogCopy
{
    size_t size;
    mwinDialogKind kind;
    const char* title;
    const char* folder;
    const char* name;
    uint32_t titleLength;
    uint32_t folderLength;
    uint32_t nameLength;
    uint32_t filterCount;
    mwinDialogFilter filters[];
} mwinDialogCopy;

// Starts gathering a dialog's paths, forgetting any gathered.
void mwinBeginDialog(mwinContext* context);

// Adds a path the dialog chose. One past the limits makes the answer
// too large; one that is not a path of UTF-8 (of UTF-16) failed.
void mwinAddDialogFile(mwinContext* context, const char* path, size_t length);
void mwinAddDialogFileUtf16(mwinContext* context, const uint16_t* path, size_t length);

// The answer to a dialog's request from how it ended and the paths
// gathered: done keeps the paths under the request's id, too large or
// failed when they did not all fit or were not paths. The caller
// completes the request with it.
mwinOutcome mwinSettleDialog(mwinContext* context, uint32_t slot, uint32_t request,
                             mwinOutcome outcome);

#endif // MAUL_WINDOW_SRC_DIALOG_H
