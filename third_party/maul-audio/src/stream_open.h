// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Opening a stream of one direction on the backend: its starting
// device, its format and period, and the platform's side.

#ifndef MAUL_AUDIO_SRC_STREAM_OPEN_H
#define MAUL_AUDIO_SRC_STREAM_OPEN_H

#include "context_core.h"

// Opens a stream from a checked def of one direction in a free slot and
// marks it live. maud_errorInvalid when its device is of the other
// direction (the caller counts the misuse), maud_errorStale for a
// device that is gone, maud_errorCapacity without a free slot, or the
// backend's refusal; the slot is untouched on failure. duplexGroup
// numbers the duplex pair the stream is a half of, or is 0.
maudResult maudOpenStream(maudContext* context, const maudStreamDef* def, uint32_t duplexGroup,
                          maudStreamSlot** slotOut);

#endif // MAUL_AUDIO_SRC_STREAM_OPEN_H
