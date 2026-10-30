// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// A context held by a call of another object that reads it.

#include "context_hold.h"

#include "context.h"

bool muiHoldContext(const muiContext* context)
{
    if (context == nullptr)
    {
        return false;
    }
    // The context is the library's own, made writable, so its mark may be
    // set through the const pointer a text host carries.
    muiContext* held = (muiContext*)context;
    bool was = held->busy;
    held->busy = true;
    return was;
}

void muiReleaseContext(const muiContext* context, bool held)
{
    if (context != nullptr)
    {
        ((muiContext*)context)->busy = held;
    }
}
