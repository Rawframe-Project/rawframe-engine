// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Channel layouts against Windows speaker masks. Plain integers, so it
// builds and is tested on every platform.

#ifndef MAUL_AUDIO_SRC_WASAPI_FORMAT_H
#define MAUL_AUDIO_SRC_WASAPI_FORMAT_H

#include "maul-audio/layout.h"

#include <stdint.h>

// The speaker mask of a layout: its speakers' SPEAKER_* bits; mono is
// the front center. 0 for maud_layoutNone.
uint32_t maudWasapiMaskOfLayout(maudChannelLayout layout);

#endif // MAUL_AUDIO_SRC_WASAPI_FORMAT_H
