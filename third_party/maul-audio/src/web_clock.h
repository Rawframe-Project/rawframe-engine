// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The web streams' clock filter. Through a PulseAudio server a browser's
// output timestamp drifts late for seconds and then snaps back, while the
// frames reach the device evenly, and comes back to its low only every
// tens of seconds (research in the docs): the earliest time a stream's
// frame 0 is heard or was captured, over the last 30 seconds, is the one
// that holds. A time well past the drift is frames lost before the
// library, which it counts.

#ifndef MAUL_AUDIO_SRC_WEB_CLOCK_H
#define MAUL_AUDIO_SRC_WEB_CLOCK_H

#include "context_core.h"

#include <stdint.h>

// The filter's memory: MAUD_WEB_CLOCK_SPANS spans of
// MAUD_WEB_CLOCK_SPAN_SECONDS each, 30 seconds.
#define MAUD_WEB_CLOCK_SPANS        6
#define MAUD_WEB_CLOCK_SPAN_SECONDS 5

// The earliest frame-0 time of each of the last spans, by the span of
// the host clock it was taken in.
typedef struct maudWebClock
{
    int64_t span[MAUD_WEB_CLOCK_SPANS];
    int64_t earliest[MAUD_WEB_CLOCK_SPANS];
} maudWebClock;

// Forgets every span: the stream started again, ran short or moved.
void maudWebResetClock(maudWebClock* clock);

// Takes a frame-0 time from a timestamp taken at now, both in
// nanoseconds, and returns the earliest of the last 30 seconds.
int64_t maudWebEarliest(maudWebClock* clock, int64_t origin, int64_t now);

// Stamps a stream's clock with a latency for the frame at its position,
// against one reading of the host clock. A latency from the browser's
// timestamp (stamped) goes through the earliest time frame 0 is heard
// (an output) or was captured (an input) over the last 30 seconds,
// and the host time is that frame-0 time plus the frame's place; a
// route epoch other than *clockEpoch starts the filter again. A frame-0
// time 50 ms later than the earliest is frames the browser lost before
// the stream: it counts an xrun and starts the filter there.
void maudWebStampClock(maudWebClock* clock, int* clockEpoch, maudStreamCore* core, int64_t latency,
                       bool stamped, int epoch, bool output);

#endif // MAUL_AUDIO_SRC_WEB_CLOCK_H
