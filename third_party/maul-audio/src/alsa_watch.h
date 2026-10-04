// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Watching a device directory for sound card nodes coming, going and
// becoming readable, without blocking.

#ifndef MAUL_AUDIO_SRC_ALSA_WATCH_H
#define MAUL_AUDIO_SRC_ALSA_WATCH_H

#include <stdbool.h>

// A non-blocking watch on directory, or -1 when it cannot be watched.
int maudAlsaOpenWatch(const char* directory);

// Reads every pending event of the watch; true when one concerned a
// card's control or PCM node.
bool maudAlsaTakeChanges(int watch);

// Ends the watch. Nothing for -1.
void maudAlsaCloseWatch(int watch);

#endif // MAUL_AUDIO_SRC_ALSA_WATCH_H
