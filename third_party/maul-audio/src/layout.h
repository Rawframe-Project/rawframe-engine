// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// What backends need from the layouts: the layout a platform's channel
// count stands for.

#ifndef MAUL_AUDIO_SRC_LAYOUT_H
#define MAUL_AUDIO_SRC_LAYOUT_H

#include "maul-audio/layout.h"

// The layout with this many channels, or maud_layoutNone.
maudChannelLayout maudLayoutWithChannels(uint32_t channels);

#endif // MAUL_AUDIO_SRC_LAYOUT_H
