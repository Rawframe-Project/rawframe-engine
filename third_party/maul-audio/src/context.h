// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// What the stream module needs from the context: its memory, its
// stream table, misuse counting, and whether the calling thread is
// rendering one of its streams.

#ifndef MAUL_AUDIO_SRC_CONTEXT_H
#define MAUL_AUDIO_SRC_CONTEXT_H

#include "context_core.h"

// Allocates through the context's allocator. Debug and test builds trap
// when the calling thread is rendering one of the context's streams.
void* maudContextAllocate(maudContext* context, size_t size, size_t alignment);

// Returns memory maudContextAllocate gave.
void maudContextRelease(maudContext* context, void* memory, size_t size, size_t alignment);

// Whether the calling thread is rendering one of the context's streams.
bool maudIsRenderingThread(const maudContext* context);

// Counts one misuse against the context.
void maudCountMisuse(maudContext* context);

// The live stream an id names, or NULL for a stale or null id.
maudStreamSlot* maudFindStream(const maudContext* context, maudStreamId stream);

// The live device an id names, or NULL for a stale or null id.
maudDeviceSlot* maudFindDevice(const maudContext* context, maudDeviceId device);

// A free slot, or NULL when every slot holds a stream.
maudStreamSlot* maudFindFreeStreamSlot(const maudContext* context);

// The id of a live slot.
maudStreamId maudStreamIdOf(const maudContext* context, const maudStreamSlot* slot);

// Frees what a live stream holds and makes its id stale.
void maudReleaseStream(maudContext* context, maudStreamSlot* slot);

#endif // MAUL_AUDIO_SRC_CONTEXT_H
