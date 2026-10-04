// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The web backend's state, shared by its playback and capture modules.

#ifndef MAUL_AUDIO_SRC_WEB_CORE_H
#define MAUL_AUDIO_SRC_WEB_CORE_H

#include "context_core.h"
#include "device.h"

#include <stddef.h>

// The most frames a stream keeps ahead of the speaker or holds from the
// microphone: its ring's capacity, a power of two, and the frames one
// render or capture moves at most.
#define MAUD_WEB_CAPACITY 4096u
// The render quantum of Web Audio, and the step of every render.
#define MAUD_WEB_QUANTUM 128u

// A stream's node, by its handle in the JavaScript table, and the chunk
// the main thread renders into or captures from.
typedef struct maudWebStream
{
    int node;
    float* chunk;
    size_t chunkBytes;
} maudWebStream;

// Bytes of a listed device's id and label.
#define MAUD_WEB_KEY_BYTES  128
#define MAUD_WEB_NAME_BYTES 128

// A listed device's id and label, which its spec points into.
typedef struct maudWebEndpoint
{
    char key[MAUD_WEB_KEY_BYTES];
    char name[MAUD_WEB_NAME_BYTES];
} maudWebEndpoint;

typedef struct maudWeb
{
    int handle;
    maudWebStream* streams;
    // Room for the browser's list: one endpoint and spec per device.
    maudWebEndpoint* endpoints;
    maudDeviceSpec* specs;
    uint32_t endpointCapacity;
    // The AudioContext's rate, which every device runs at.
    uint32_t rate;
    size_t bytes;
} maudWeb;

#endif // MAUL_AUDIO_SRC_WEB_CORE_H
