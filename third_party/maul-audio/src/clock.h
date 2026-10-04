// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The host clock and a stream's clock stamp. The rendering thread
// stamps the stream each callback; any thread reads the stamp, without
// a lock, under a sequence counter.

#ifndef MAUL_AUDIO_SRC_CLOCK_H
#define MAUL_AUDIO_SRC_CLOCK_H

#include "context_core.h"

#include <stdint.h>

// Nanoseconds on the platform's monotonic clock.
int64_t maudNowNanoseconds(void);

// Stamps an output stream: the frame at its position before this
// callback's frames is heard latency nanoseconds from now. Real-time
// safe.
void maudStampOutputClock(maudStreamCore* core, int64_t latency);

// Stamps an input stream: the first frame of this callback's frames,
// at its position before them, was captured latency nanoseconds ago.
// Real-time safe.
void maudStampInputClock(maudStreamCore* core, int64_t latency);

// Clears a new stream's stamp: no host time yet.
void maudResetClock(maudStreamCore* core);

// Reads the stamp: its position, host time (0 before the first stamp)
// and latency.
void maudReadClock(const maudStreamCore* core, uint64_t* position, int64_t* host, int64_t* latency);

#endif // MAUL_AUDIO_SRC_CLOCK_H
