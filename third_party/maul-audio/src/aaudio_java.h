// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The AAudio backend's Java half: the library's maul.audio.Devices
// object, reached through JNI from the control thread only.

#ifndef MAUL_AUDIO_SRC_AAUDIO_JAVA_H
#define MAUL_AUDIO_SRC_AAUDIO_JAVA_H

#include "aaudio_core.h"

// One device Java listed: its AAudio id, its AudioDeviceInfo type, its
// most channels and its rate range (0 where it takes any), its address
// and product name.
typedef struct maudAaudioListing
{
    int32_t id;
    int32_t type;
    int32_t channels;
    int32_t lowRate;
    int32_t highRate;
    const char* address;
    const char* product;
} maudAaudioListing;

// Loads the Devices class through the Context's class loader, registers
// its native methods and makes the object, which raises
// aaudio->signals.changed whenever Android's devices change and stores
// each focus change in aaudio->signals.focus. false when any of it
// fails.
bool maudAaudioOpenJava(maudAaudio* aaudio, void* vm, void* context);

// Closes the object and drops the references.
void maudAaudioCloseJava(maudAaudio* aaudio);

// Calls listed for each device of a direction Java lists.
void maudAaudioListJava(maudAaudio* aaudio, maudDirection direction,
                        void (*listed)(maudAaudio* aaudio, maudDirection direction,
                                       const maudAaudioListing* listing));

// Whether the application holds the microphone permission; true when
// Java cannot say, so that AAudio's own refusal stands.
bool maudAaudioMayRecord(maudAaudio* aaudio);

// Asks for the microphone, once, where the Context is an Activity.
void maudAaudioAskToRecord(maudAaudio* aaudio);

// Asks for audio focus (1 lasting, 2 brief, 3 brief and mixed) or gives
// it back (0), for a call or for media: AudioManager's result, 0
// refused, 1 granted, 2 delayed; 0 when Java cannot answer.
int32_t maudAaudioRequestFocusJava(maudAaudio* aaudio, int32_t kind, bool call);

// Android's Spatializer on the current route: 0 where there is none,
// otherwise 1 plus 2 where the route can be spatialized, 4 where the user
// turned it on and 8 where a head tracker is available; -1 when Java
// cannot answer.
int32_t maudAaudioSpatializerJava(maudAaudio* aaudio);

#endif // MAUL_AUDIO_SRC_AAUDIO_JAVA_H
