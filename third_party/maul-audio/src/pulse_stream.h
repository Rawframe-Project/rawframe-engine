// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Streams on PulseAudio: the backend's stream entry points.

#ifndef MAUL_AUDIO_SRC_PULSE_STREAM_H
#define MAUL_AUDIO_SRC_PULSE_STREAM_H

#include "context_core.h"

// Connects the stream corked on its own context, waiting up to the
// deadline; while the server is away it waits without one.
maudResult maudPulseAttachStream(maudContext* context, maudStreamSlot* slot);

// Joins the stream's thread and releases its connection.
void maudPulseDetachStream(maudContext* context, maudStreamSlot* slot);

// Starts the stream's thread, uncorked, rebuilding a connection that
// died; or joins the thread and corks the stream.
void maudPulseSetStreamActive(maudContext* context, maudStreamSlot* slot, bool active);

// Rebuilds the stream at its new rate, running again if it ran.
void maudPulseRetargetStream(maudContext* context, maudStreamSlot* slot);

// Rebuilds the started streams the server ended, or that have no
// connection or no thread, once their retry is due.
void maudPulseResumeStreams(maudContext* context);

// While the server is away: every started stream not suspended yet is
// suspended for the platform until a rebuild on a new server works.
void maudPulseSuspendStreams(maudContext* context);

#endif // MAUL_AUDIO_SRC_PULSE_STREAM_H
