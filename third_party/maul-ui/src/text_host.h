// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Entering a call given a muiTextHost: the service marked busy and the
// context held for the whole call, so host code the call runs changes
// neither while the call reads them.

#ifndef MAUL_UI_SRC_TEXT_HOST_H
#define MAUL_UI_SRC_TEXT_HOST_H

#include "context_hold.h"
#include "text_service.h"

#include "maul-ui/text_block.h"

#include <stdbool.h>

// Enters a call given a host, NULL or not: mui_success with the context's
// hold in heldOut, or mui_errorBusy as muiEnterText answers.
static inline muiResult muiEnterTextHost(const muiTextHost* host, bool* heldOut)
{
    muiResult status = muiEnterText(host != nullptr ? host->service : nullptr);
    *heldOut = status == mui_success && host != nullptr && muiHoldContext(host->context);
    return status;
}

// Ends what muiEnterTextHost began.
static inline void muiLeaveTextHost(const muiTextHost* host, bool held)
{
    if (host != nullptr)
    {
        muiReleaseContext(host->context, held);
        muiLeaveText(host->service);
    }
}

#endif
