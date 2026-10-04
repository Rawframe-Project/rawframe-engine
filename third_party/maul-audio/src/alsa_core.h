// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The ALSA backend's state, shared by its devices and its streams.

#ifndef MAUL_AUDIO_SRC_ALSA_CORE_H
#define MAUL_AUDIO_SRC_ALSA_CORE_H

#include "alsa_api.h"
#include "alsa_chmap.h"
#include "alsa_scan.h"
#include "context_core.h"
#include "worker.h"

#include <poll.h>
#include <stdatomic.h>

// The rate a native stream asks the hardware for, which takes the
// nearest it has.
#define MAUD_ALSA_PREFERRED_RATE 48000u
// Bytes of a PCM name: "plughw:CARD=<id>,DEV=<n>".
#define MAUD_ALSA_NAME_BYTES 64

// A stream's PCM, run by its thread while the stream runs and by the
// control thread otherwise, never both at once.
typedef struct maudAlsaStream
{
    const maudAlsaApi* api;
    maudStreamCore* core;
    snd_pcm_t* pcm;
    // The eventfd first, then the PCM's descriptors.
    struct pollfd* fds;
    uint32_t fdCount;
    // A device buffer's worth of frames in the stream's order, and in the
    // PCM's when the two differ.
    float* samples;
    float* reordered;
    uint32_t bufferFrames;
    // Rendered frames a write left for the next one, from pendingOffset.
    uint32_t pendingOffset;
    uint32_t pendingFrames;
    uint8_t order[MAUD_ALSA_MAX_CHANNELS];
    size_t bytes;
    maudWorker worker;
    bool threadRunning;
} maudAlsaStream;

typedef struct maudAlsa
{
    maudAlsaApi api;
    maudContext* context;
    maudAlsaStream* streams;
    // Room for a scan of as many endpoints as the context has devices.
    maudAlsaEndpoint* endpoints;
    maudDeviceSpec* specs;
    // The watch on the card nodes, or -1.
    int watch;
    size_t bytes;
} maudAlsa;

// Silences alsa-lib's messages on the calling thread; returns the
// handler to restore.
snd_local_error_handler_t maudAlsaQuiet(const maudAlsaApi* api);

#endif // MAUL_AUDIO_SRC_ALSA_CORE_H
