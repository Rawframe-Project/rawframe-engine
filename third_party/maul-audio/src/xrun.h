// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Counting the underruns and overruns a platform reveals, on its thread.

#ifndef MAUL_AUDIO_SRC_XRUN_H
#define MAUL_AUDIO_SRC_XRUN_H

#include "context_core.h"

// An output played without the stream's frames: an underrun; an input
// lost frames before the stream got them: an overrun. Either is counted
// by the stream's direction. Real-time safe.
static inline void maudCountXrun(maudStreamCore* core)
{
    _Atomic(uint64_t)* counter =
        core->def.direction == maud_directionOutput ? &core->underruns : &core->overruns;
    atomic_fetch_add_explicit(counter, 1, memory_order_relaxed);
}

#endif // MAUL_AUDIO_SRC_XRUN_H
