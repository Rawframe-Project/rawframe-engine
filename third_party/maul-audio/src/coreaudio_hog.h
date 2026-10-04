// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// CoreAudio's hog mode: a device's IO for one process alone, which an
// exclusive stream takes and gives back.

#ifndef MAUL_AUDIO_SRC_COREAUDIO_HOG_H
#define MAUL_AUDIO_SRC_COREAUDIO_HOG_H

#include "coreaudio_core.h"

// Takes object for this process: maud_errorUnsupported when the device
// has no hog mode, maud_errorPlatform when a process (this one
// included, for another stream) holds it already.
maudResult maudCoreAudioTakeDevice(AudioObjectID object);

// Gives object back, if this process holds it.
void maudCoreAudioGiveDevice(AudioObjectID object);

#endif // MAUL_AUDIO_SRC_COREAUDIO_HOG_H
