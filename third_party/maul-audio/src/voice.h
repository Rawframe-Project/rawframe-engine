// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The platform's report of a stream's voice processing.

#ifndef MAUL_AUDIO_SRC_VOICE_H
#define MAUL_AUDIO_SRC_VOICE_H

#include "context_core.h"

// Clears the report: nothing reported yet.
void maudResetVoice(maudStreamCore* core);

// Records which parts of voice processing the platform says are active.
// Callable from any thread.
void maudReportVoice(maudStreamCore* core, maudVoiceProcessing active);

#endif // MAUL_AUDIO_SRC_VOICE_H
