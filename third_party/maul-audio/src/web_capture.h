// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Capture on the web: getUserMedia's track into a capture worklet, its
// frames moved to the main thread and pushed through the stream.

#ifndef MAUL_AUDIO_SRC_WEB_CAPTURE_H
#define MAUL_AUDIO_SRC_WEB_CAPTURE_H

#include "context_core.h"

// Registers the capture processor on the context's AudioContext.
void maudWebAddCaptureProcessor(int handle);

// Asks for the microphone and, once granted, connects the stream's
// capture node; the stream waits with maud_suspendPermission until
// then. Returns the node's handle.
int maudWebOpenCapture(maudContext* context, maudStreamSlot* slot, int handle, float* chunk);

// Disconnects a capture node and stops its track.
void maudWebCloseCapture(int node);

#endif // MAUL_AUDIO_SRC_WEB_CAPTURE_H
