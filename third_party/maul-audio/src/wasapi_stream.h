// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Streams on WASAPI: the backend's stream entry points.

#ifndef MAUL_AUDIO_SRC_WASAPI_STREAM_H
#define MAUL_AUDIO_SRC_WASAPI_STREAM_H

#include "context_core.h"

// Opens a shared-mode audio client on the stream's device; with no
// device, the stream waits without one. maud_errorUnsupported for a
// format WASAPI refuses, maud_errorPlatform for other failures.
maudResult maudWasapiAttachStream(maudContext* context, maudStreamSlot* slot);

// Joins the stream's thread and releases its client.
void maudWasapiDetachStream(maudContext* context, maudStreamSlot* slot);

// Starts the stream's thread, opening the client again if it failed;
// or stops the thread and the client.
void maudWasapiSetStreamActive(maudContext* context, maudStreamSlot* slot, bool active);

// Opens the client again on the stream's device and rate, running
// again if it ran.
void maudWasapiRetargetStream(maudContext* context, maudStreamSlot* slot);

// Whether a running stream's thread ended on a failure, or it has no
// client: what the next call opens again.
bool maudWasapiStreamsToResume(maudContext* context);

// Opens again every running stream whose thread ended on a failure.
void maudWasapiResumeStreams(maudContext* context);

#endif // MAUL_AUDIO_SRC_WASAPI_STREAM_H
