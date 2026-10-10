// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The plain-integer parts of WASAPI: channel layouts against Windows
// speaker masks, and performance-counter times as host time. Plain
// integers, so they build and are tested on every platform.

#ifndef MAUL_AUDIO_SRC_WASAPI_FORMAT_H
#define MAUL_AUDIO_SRC_WASAPI_FORMAT_H

#include "maul-audio/layout.h"

#include <stdint.h>

// The speaker mask of a layout: its speakers' SPEAKER_* bits; mono is
// the front center. 0 for maud_layoutNone.
uint32_t maudWasapiMaskOfLayout(maudChannelLayout layout);

// A performance-counter time in 100-nanosecond units as host
// nanoseconds, or fallback when it is 0 or more than a second from now
// (wine's counter times overflow).
int64_t maudWasapiCounterTime(uint64_t counter, int64_t now, int64_t fallback);

#endif // MAUL_AUDIO_SRC_WASAPI_FORMAT_H
