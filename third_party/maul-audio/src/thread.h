// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The identity of the calling thread, so the library can tell a thread
// that is rendering a stream from every other.

#ifndef MAUL_AUDIO_SRC_THREAD_H
#define MAUL_AUDIO_SRC_THREAD_H

#include <stdint.h>

// A value that names the calling thread while it runs, never 0.
uintptr_t maudCurrentThread(void);

#endif // MAUL_AUDIO_SRC_THREAD_H
