// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// What the PipeWire backend reads out of the daemon's objects and says
// to it, apart from the connection: a format choice's default, a node's
// card, a default's node name, the graph's rate, and a stream's format
// and media properties.

#ifndef MAUL_AUDIO_SRC_PIPEWIRE_PARSE_H
#define MAUL_AUDIO_SRC_PIPEWIRE_PARSE_H

#include "maul-audio/device.h"
#include "maul-audio/layout.h"
#include "maul-audio/stream.h"

#include <spa/pod/pod.h>
#include <spa/utils/dict.h>
#include <stddef.h>
#include <stdint.h>

// The default of an Int value or choice; 0 for another type or none.
uint32_t maudPipewireChoiceDefault(const struct spa_pod* value);

// Whether a node's properties put it on a card's profile device, and
// which: both the card's id and the profile device must read whole.
bool maudPipewireNodeCard(const struct spa_dict* props, uint32_t* cardIdOut,
                          int32_t* profileDeviceOut);

// Reads the node name out of a default metadata value, {"name": "..."},
// into name, of nameBytes; empty when there is none.
void maudPipewireParseDefaultName(const char* value, char* name, size_t nameBytes);

// The graph's rate: the forced rate, else the clock's, else fallback.
uint32_t maudPipewireGraphRate(uint32_t forceRate, uint32_t clockRate, uint32_t fallback);

// Builds a stream's format into buffer, of bytes: interleaved 32-bit
// float at rate, with the layout's positions; a mono stream is MONO.
const struct spa_pod* maudPipewireBuildFormat(maudChannelLayout layout, uint32_t rate,
                                              uint8_t* buffer, size_t bytes);

// A stream's media category, Playback or Capture, and its role.
const char* maudPipewireMediaCategory(maudDirection direction);
const char* maudPipewireMediaRole(maudDeviceRole role);

#endif
