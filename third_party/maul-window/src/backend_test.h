// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// What the test backend keeps, shared by backend_test.c and the
// services it plays (backend_test_services.c).

#ifndef MAUL_WINDOW_SRC_BACKEND_TEST_H
#define MAUL_WINDOW_SRC_BACKEND_TEST_H

#include "backend.h"
#include "core.h"

#include "maul-window/services.h"

#define MWIN_TEST_KINDS (mwin_requestAccessibilityRoot + 1)

// The bytes of the paths a dialog chooses, and of the last dialog's
// description.
#define MWIN_TEST_DIALOG_BYTES 1024

// A request waiting for the next pump. The generations tell it from a
// later window or request in the same slots.
typedef struct mwinTestPending
{
    uint32_t slot;
    uint32_t request;
    uint32_t windowGeneration;
    uint32_t requestGeneration;
} mwinTestPending;

// Reports and their text waiting for the next pump.
#define MWIN_TEST_REPORTS         1024
#define MWIN_TEST_REPORT_TEXT     65536
#define MWIN_TEST_REPORT_SEGMENTS 1024

// A gamepad's last rumble, and how many there were.
typedef struct mwinTestRumble
{
    float low;
    float high;
    uint32_t durationMs;
    uint32_t count;
} mwinTestRumble;

typedef struct mwinTestPlatform
{
    mwinTestPending* pending;
    // One per gamepad slot.
    mwinTestRumble* rumbles;
    uint32_t pendingCount;
    uint32_t pendingCapacity;
    mwinEvent reports[MWIN_TEST_REPORTS];
    uint32_t reportCount;
    char reportText[MWIN_TEST_REPORT_TEXT];
    uint32_t reportTextUsed;
    mwinPreeditSegment reportSegments[MWIN_TEST_REPORT_SEGMENTS];
    uint32_t reportSegmentsUsed;
    mwinOutcome answers[MWIN_TEST_KINDS];
    // The platform's clipboard: bytes, or UTF-16 units when utf16 is set,
    // in a block of their own from the allocator.
    void* clipboard;
    size_t clipboardBytes;
    bool utf16;
    // A drop gathered in the context waits for its report.
    bool dropWaiting;
    // The last address opened and path revealed.
    char opened[2][MWIN_ADDRESS_BYTES];
    uint32_t openedLength[2];
    char dialogFiles[MWIN_TEST_DIALOG_BYTES];
    uint32_t dialogFilesLength;
    char dialog[MWIN_TEST_DIALOG_BYTES];
    uint32_t dialogLength;
    uint32_t iconCount;
    uint64_t iconChecksum;
    bool hold;
    uint64_t timeNs;
    float scale;
} mwinTestPlatform;

// The test platform of a context of the test backend, or NULL.
static inline mwinTestPlatform* mwinTestPlatformOf(const mwinContext* context)
{
    return context != nullptr && context->backend == &mwinTestBackend
               ? (mwinTestPlatform*)context->backendData
               : nullptr;
}

// Copies a report, its text and its segments into the platform's queue.
mwinResult mwinTestQueueReport(mwinTestPlatform* platform, const mwinEvent* event);

// Uses the clipboard as a platform would: a write replaces its text, a
// read takes it.
mwinOutcome mwinTestUseClipboard(mwinContext* context, mwinRequestKind kind);

// Opens an address or reveals a path as a platform would: it keeps it.
void mwinTestOpen(mwinContext* context, const mwinRequest* request);

// Writes the icon down.
void mwinTestSetIcon(mwinContext* context, const mwinRequest* request);

// Writes the dialog down, and answers it with the paths set: the
// outcome, given how it ended.
mwinOutcome mwinTestAnswerDialog(mwinContext* context, uint32_t slot, uint32_t request,
                                 mwinOutcome outcome);

// Gives the platform's clipboard back; the backend's stop calls it.
void mwinTestReleaseClipboard(const mwinContext* context, mwinTestPlatform* platform);

#endif // MAUL_WINDOW_SRC_BACKEND_TEST_H
