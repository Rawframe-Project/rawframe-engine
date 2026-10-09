// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Streams on iOS: a RemoteIO unit per stream.

#ifndef MAUL_AUDIO_SRC_IOS_STREAM_H
#define MAUL_AUDIO_SRC_IOS_STREAM_H

#include "context_core.h"

// Makes and initializes the stream's RemoteIO unit.
maudResult maudIosAttachStream(maudContext* context, maudStreamSlot* slot);

// Stops and disposes of the stream's unit; afterwards the IO thread no
// longer touches the stream.
void maudIosDetachStream(maudContext* context, maudStreamSlot* slot);

// Starts or stops the stream's unit, setting the session for what runs.
void maudIosSetStreamActive(maudContext* context, maudStreamSlot* slot, bool active);

// Sets the session for the streams there are and those that run, and
// keeps it active while making a unit (preparing), as iOS initializes an
// input unit only in an active session; false when it refuses.
bool maudIosUpdateSession(maudContext* context, bool preparing);

#endif // MAUL_AUDIO_SRC_IOS_STREAM_H
