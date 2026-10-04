// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Posting to the context's notification queue.

#ifndef MAUL_AUDIO_SRC_NOTIFY_H
#define MAUL_AUDIO_SRC_NOTIFY_H

#include "context_core.h"

// Appends a record. When only one place is left, an overflow record
// takes it; past that, the overflow record counts what is dropped.
void maudPostNotification(maudContext* context, const maudNotification* record);

#endif // MAUL_AUDIO_SRC_NOTIFY_H
