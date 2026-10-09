// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The reads of the clipboard and the primary selection as the X11 and
// Wayland backends run them (mwin-0029): answered at once from what the
// program owns, or through the other client one at a time, the others
// waiting their turn. A read answers every window's request of its kind,
// and for data of its type; a window has one request of a kind at most.

#ifndef MAUL_WINDOW_SRC_SELECTION_READS_H
#define MAUL_WINDOW_SRC_SELECTION_READS_H

#include "core.h"

#include "maul-window/clipboard.h"

// A read under way: the kind of request it answers, and a data read's
// MIME type with a NUL after it.
typedef struct mwinSelectionRead
{
    mwinRequestKind kind;
    char mime[MWIN_CLIPBOARD_MIME + 1];
    uint32_t mimeLength;
} mwinSelectionRead;

// Whether a request reads the primary selection rather than the
// clipboard.
bool mwinReadsPrimary(const mwinRequest* request);

// Keeps what a read for a request is of.
void mwinBeginSelectionRead(mwinSelectionRead* read, const mwinRequest* request);

// Answers a read of what the program owns from its own copy; a type the
// data written lacks fails.
mwinOutcome mwinAnswerOwnRead(mwinContext* context, const mwinRequest* request);

// Holds what a read found for the program, or, with no bytes at all,
// what it finds when the other client has nothing of the type: empty
// text, or failed data. The outcome.
mwinOutcome mwinTakeSelectionRead(mwinContext* context, const mwinSelectionRead* read,
                                  const void* bytes, size_t length);
mwinOutcome mwinMissedSelectionRead(mwinContext* context, const mwinSelectionRead* read);

// Completes every window's request a read answers.
void mwinFinishSelectionRead(mwinContext* context, const mwinSelectionRead* read,
                             mwinOutcome outcome);

// Starts the reads that waited, in window order, through a backend's
// start, which answers a request (its outcome, the request completed) or
// takes it on or finds a read running (-1, the request left to wait);
// once one read runs, the others wait, but those the program's own copy
// answers.
typedef int (*mwinStartSelectionRead)(void* backend, const mwinRequest* request);
void mwinStartWaitingReads(mwinContext* context, mwinStartSelectionRead start, void* backend);

#endif // MAUL_WINDOW_SRC_SELECTION_READS_H
