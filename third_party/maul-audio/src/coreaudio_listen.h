// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The context's listeners on the system object: the device list, the
// defaults and the audio server's restarts.

#ifndef MAUL_AUDIO_SRC_COREAUDIO_LISTEN_H
#define MAUL_AUDIO_SRC_COREAUDIO_LISTEN_H

#include "coreaudio_core.h"

// Makes the change block, on the context's queue, and adds it to the
// system object's properties. A change raises the changed flag; a
// restart of the audio server raises the restarted flag too.
void maudCoreAudioListen(maudCoreAudio* coreaudio);

// After a restart of the audio server, which forgets a client's
// listeners, removes the system object's and the data sources' and adds
// the system object's again (the next rescan adds the data sources');
// false, doing nothing, when no restart was seen since the last call.
bool maudCoreAudioRelisten(maudCoreAudio* coreaudio);

// Removes every listener and waits out a change block in flight.
void maudCoreAudioStopListening(maudCoreAudio* coreaudio);

#endif // MAUL_AUDIO_SRC_COREAUDIO_LISTEN_H
