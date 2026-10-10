// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Streams on ALSA: the backend's stream entry points.

#ifndef MAUL_AUDIO_SRC_ALSA_STREAM_H
#define MAUL_AUDIO_SRC_ALSA_STREAM_H

#include "alsa_api.h"
#include "context_core.h"

// Opens the stream's PCM and sets its parameters; a native stream takes
// the hardware's rate. maud_errorUnsupported for a required rate or a
// layout the PCM cannot run, maud_errorPlatform when it cannot be
// opened.
maudResult maudAlsaAttachStream(maudContext* context, maudStreamSlot* slot);

// Joins the stream's thread and closes its PCM.
void maudAlsaDetachStream(maudContext* context, maudStreamSlot* slot);

// Prepares the PCM and starts the stream's thread, or stops the thread
// and drops what the PCM had queued.
void maudAlsaSetStreamActive(maudContext* context, maudStreamSlot* slot, bool active);

// After a transfer failed with result (negative, not -EAGAIN): counts an
// xrun for -EPIPE, recovers the PCM, and starts a capture again. False
// when the PCM failed past recovery, as when its card went away.
bool maudAlsaRecover(const maudAlsaApi* api, snd_pcm_t* pcm, maudStreamCore* core,
                     snd_pcm_sframes_t result);

#endif // MAUL_AUDIO_SRC_ALSA_STREAM_H
