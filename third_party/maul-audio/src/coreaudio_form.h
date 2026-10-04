// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// What a CoreAudio device leads to: its data source, else its
// transport, and the listeners that see its data source change.

#ifndef MAUL_AUDIO_SRC_COREAUDIO_FORM_H
#define MAUL_AUDIO_SRC_COREAUDIO_FORM_H

#include "coreaudio_core.h"

// The form of one direction of a device: its data source in that scope
// (internal or external speaker, headphones, microphone, line, S/PDIF),
// else digital for an HDMI or DisplayPort transport, else unknown.
maudDeviceForm maudCoreAudioFormOf(AudioObjectID object, maudDirection direction);

// Listens to the data source of each of the scan's count endpoints that
// has one, and stops listening to those no longer scanned. A change
// raises the context's changed flag, as a device list change does.
void maudCoreAudioWatchSources(maudCoreAudio* coreaudio, uint32_t count);

// Stops listening to every data source.
void maudCoreAudioUnwatchSources(maudCoreAudio* coreaudio);

#endif // MAUL_AUDIO_SRC_COREAUDIO_FORM_H
