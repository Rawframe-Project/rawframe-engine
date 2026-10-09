// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Streams on AAudio: one AAudio stream per stream.

#ifndef MAUL_AUDIO_SRC_AAUDIO_STREAM_H
#define MAUL_AUDIO_SRC_AAUDIO_STREAM_H

#include "context_core.h"

// Opens the stream's AAudio stream on the default device of its
// direction.
maudResult maudAaudioAttachStream(maudContext* context, maudStreamSlot* slot);

// Closes the stream's AAudio stream; afterwards AAudio's thread no
// longer touches the stream.
void maudAaudioDetachStream(maudContext* context, maudStreamSlot* slot);

// Starts or stops the stream's AAudio stream.
void maudAaudioSetStreamActive(maudContext* context, maudStreamSlot* slot, bool active);

// Opens again every stream AAudio lost, and every running stream that
// has no AAudio stream, and starts those that run.
void maudAaudioResumeStreams(maudContext* context);

#endif // MAUL_AUDIO_SRC_AAUDIO_STREAM_H
