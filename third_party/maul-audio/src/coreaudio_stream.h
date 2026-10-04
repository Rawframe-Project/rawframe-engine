// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Streams on CoreAudio: an AUHAL output unit per stream.

#ifndef MAUL_AUDIO_SRC_COREAUDIO_STREAM_H
#define MAUL_AUDIO_SRC_COREAUDIO_STREAM_H

#include "context_core.h"

// Opens the stream's unit on its device, if it has one.
maudResult maudCoreAudioAttachStream(maudContext* context, maudStreamSlot* slot);

// Stops and disposes of the stream's unit; afterwards the IO thread no
// longer touches the stream.
void maudCoreAudioDetachStream(maudContext* context, maudStreamSlot* slot);

// Starts or stops the stream's unit.
void maudCoreAudioSetStreamActive(maudContext* context, maudStreamSlot* slot, bool active);

// Opens the stream's unit again on its current device.
void maudCoreAudioRetargetStream(maudContext* context, maudStreamSlot* slot);

// Opens again every running stream that has no unit.
void maudCoreAudioResumeStreams(maudContext* context);

#endif // MAUL_AUDIO_SRC_COREAUDIO_STREAM_H
