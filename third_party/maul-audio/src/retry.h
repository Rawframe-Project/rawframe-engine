// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Retrying a stream the platform failed to reopen, on the control thread.
// A backend's drain tries again a stream left without its platform
// stream; a failure that lasts (devices the platform cannot pair, a
// service not back yet) waits longer each time, so that a drain the host
// calls in a loop never repeats the platform's work on every call.

#ifndef MAUL_AUDIO_SRC_RETRY_H
#define MAUL_AUDIO_SRC_RETRY_H

#include "context_core.h"

// The first wait after a failure, and the longest, in nanoseconds.
#define MAUD_RETRY_FIRST_NS 250000000
#define MAUD_RETRY_LAST_NS  4000000000

// Whether the stream may be tried again at now.
static inline bool maudRetryDue(const maudStreamBinding* binding, int64_t now)
{
    return now >= binding->retryAt;
}

// Records a failed try at now: the next waits twice as long as the last,
// from MAUD_RETRY_FIRST_NS up to MAUD_RETRY_LAST_NS.
static inline void maudRetryFailed(maudStreamBinding* binding, int64_t now)
{
    int64_t wait = binding->retryWait == 0 ? MAUD_RETRY_FIRST_NS : binding->retryWait * 2;
    binding->retryWait = wait < MAUD_RETRY_LAST_NS ? wait : MAUD_RETRY_LAST_NS;
    binding->retryAt = now + binding->retryWait;
}

// Records a try that worked: the next failure waits the first wait again.
static inline void maudRetrySucceeded(maudStreamBinding* binding)
{
    binding->retryWait = 0;
    binding->retryAt = 0;
}

#endif // MAUL_AUDIO_SRC_RETRY_H
