// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The context's audio focus, as its backend reports it.

#ifndef MAUL_AUDIO_SRC_FOCUS_H
#define MAUL_AUDIO_SRC_FOCUS_H

#include "context_core.h"

// Records the focus the platform reports, posting maud_notifyFocusChanged
// when it differs from the last. On the control thread.
void maudReportFocus(maudContext* context, maudFocus focus);

#endif // MAUL_AUDIO_SRC_FOCUS_H
