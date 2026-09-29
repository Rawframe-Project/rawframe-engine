// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// A pipe another Wayland client writes into, read without blocking a
// piece at each pump into a block from the context's allocator that
// grows to one byte past a limit, so text too large is told without
// taking it all.

#ifndef MAUL_WINDOW_SRC_WAYLAND_PIPE_H
#define MAUL_WINDOW_SRC_WAYLAND_PIPE_H

#include "core.h"

typedef struct mwinWaylandPipe
{
    // The reading end, or -1, and the bytes so far in a block of
    // capacity bytes.
    int fd;
    char* bytes;
    uint32_t length;
    uint32_t capacity;
} mwinWaylandPipe;

// Opens a pipe: its writing end, for a request to carry and the caller
// to close, or -1 when there is none.
int mwinWaylandOpenPipe(mwinWaylandPipe* pipe);

// Reads what the pipe has: -1 while more may come; otherwise the writer
// closed it (mwin_outcomeDone, the bytes all there), or they passed the
// limit (mwin_outcomeTooLarge), or reading failed (mwin_outcomeFailed).
int mwinWaylandReadPipe(mwinWaylandPipe* pipe, const mwinContext* context, uint32_t limit);

// Closes the pipe and gives its bytes back.
void mwinWaylandClosePipe(mwinWaylandPipe* pipe, const mwinContext* context);

#endif // MAUL_WINDOW_SRC_WAYLAND_PIPE_H
