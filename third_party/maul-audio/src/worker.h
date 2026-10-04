// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// A stream's library thread: started with a name and joined by its
// owner. On POSIX systems it asks for real-time priority; on Windows
// the backend's run function joins MMCSS. Backends whose platform runs
// no audio thread use it.

#ifndef MAUL_AUDIO_SRC_WORKER_H
#define MAUL_AUDIO_SRC_WORKER_H

#include <stdbool.h>

#if !defined(_WIN32)
#include <pthread.h>
#endif

typedef struct maudWorker
{
#if defined(_WIN32)
    void* thread;
#else
    pthread_t thread;
#endif
    void (*run)(void* user);
    void* user;
    bool running;
} maudWorker;

// Starts run(user) on a new thread named name (at most 15 bytes, ASCII);
// the name is set before this returns. False when no thread could start.
bool maudStartWorker(maudWorker* worker, void (*run)(void* user), void* user, const char* name);

// Waits for the worker's run to return. Nothing when it is not running.
void maudJoinWorker(maudWorker* worker);

#endif // MAUL_AUDIO_SRC_WORKER_H
