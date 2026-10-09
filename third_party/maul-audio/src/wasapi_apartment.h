// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The WASAPI backend's apartment thread: a thread of the context's in
// COM's multithreaded apartment, on which every MMDevice, audio client
// and spatial audio object is created, used and released, so that the
// host may call from a thread in any apartment (a main thread in a
// single-threaded one, for drag and drop, is common). A call runs on it
// while the caller waits; calls come one at a time, as the context's
// functions do.

#ifndef MAUL_AUDIO_SRC_WASAPI_APARTMENT_H
#define MAUL_AUDIO_SRC_WASAPI_APARTMENT_H

#include "worker.h"

#include <stdbool.h>

typedef struct maudWasapiApartment
{
    maudWorker worker;
    // Signalled by the caller when a call (or the end) is posted, and by
    // the thread when it has run.
    void* posted;
    void* done;
    unsigned long threadId;
    void (*call)(void* user);
    void* user;
    // The thread is in the multithreaded apartment; it is to end.
    bool joined;
    bool quit;
} maudWasapiApartment;

// Starts the thread and waits until it is in the apartment. False when
// no thread could start or it could not join the apartment.
bool maudStartWasapiApartment(maudWasapiApartment* apartment);

// Runs call(user) on the thread and returns once it has; on the thread
// itself, runs it at once.
void maudCallInWasapiApartment(maudWasapiApartment* apartment, void (*call)(void* user),
                               void* user);

// Ends the thread and joins it. Nothing when it never started.
void maudStopWasapiApartment(maudWasapiApartment* apartment);

#endif // MAUL_AUDIO_SRC_WASAPI_APARTMENT_H
