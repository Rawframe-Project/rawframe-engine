// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The request a backend answers later (a Linux service, a macOS panel):
// its window slot, request slot and generation, so an answer never goes
// to a request that took the slot since.

#ifndef MAUL_WINDOW_SRC_ANSWER_H
#define MAUL_WINDOW_SRC_ANSWER_H

#include "core.h"

typedef struct mwinServiceAnswer
{
    uint32_t slot;
    uint32_t request;
    uint32_t generation;
    bool waiting;
} mwinServiceAnswer;

static inline mwinServiceAnswer mwinAnswerTo(const mwinContext* context, uint32_t slot,
                                             uint32_t request)
{
    return (mwinServiceAnswer){slot, request, context->windows[slot].requests[request].generation,
                               true};
}

// The request, while it still waits for this answer; else NULL.
static inline mwinRequest* mwinAnswerRequest(const mwinContext* context,
                                             const mwinServiceAnswer* to)
{
    const mwinWindow* window = &context->windows[to->slot];
    mwinRequest* request = &window->requests[to->request];
    bool waiting = to->waiting && window->status == mwin_slotLive &&
                   request->status == mwin_requestActive && request->generation == to->generation;
    return waiting ? request : nullptr;
}

// Answers a request that still waits for its answer.
static inline void mwinAnswer(mwinContext* context, mwinServiceAnswer* to, mwinOutcome outcome)
{
    if (mwinAnswerRequest(context, to) != nullptr)
    {
        mwinComplete(context, to->slot, to->request, outcome);
    }
    to->waiting = false;
}

#endif // MAUL_WINDOW_SRC_ANSWER_H
