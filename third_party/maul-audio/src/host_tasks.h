// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Work run through the host's task hooks. enqueueTask and finishTask are
// host code whose results feed nothing the library promises, so they run
// in the host's own floating-point environment (fpenv.h); each range of
// the work runs in the library's, on whichever thread the host runs it,
// and leaves that thread's mode as it found it. Without hooks the work
// runs on the calling thread, inside the caller's entry.

#ifndef MAUL_AUDIO_SRC_HOST_TASKS_H
#define MAUL_AUDIO_SRC_HOST_TASKS_H

#include "maul-audio/spatializer.h"

// The host's task hooks; both NULL runs the work on the calling thread.
typedef struct maudHostTasks
{
    maudEnqueueTaskFn* enqueue;
    maudFinishTaskFn* finish;
    void* user;
} maudHostTasks;

// Runs task over [0, items) in ranges of at least one item and returns
// once every range has run.
void maudRunHostTasks(const maudHostTasks* hooks, maudTaskFn* task, uint32_t items, void* context);

#endif // MAUL_AUDIO_SRC_HOST_TASKS_H
