// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The platform's report of a stream's voice processing: the active parts
// are stored before the reported flag, which publishes them.

#include "voice.h"

void maudResetVoice(maudStreamCore* core)
{
    atomic_store_explicit(&core->voiceReported, false, memory_order_relaxed);
    atomic_store_explicit(&core->voiceActive, maud_voiceNone, memory_order_relaxed);
}

void maudReportVoice(maudStreamCore* core, maudVoiceProcessing active)
{
    atomic_store_explicit(&core->voiceActive, active, memory_order_relaxed);
    atomic_store_explicit(&core->voiceReported, true, memory_order_release);
}
