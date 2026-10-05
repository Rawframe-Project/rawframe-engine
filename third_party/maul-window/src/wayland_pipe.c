// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// A pipe another Wayland client writes into.

#include "wayland_pipe.h"

#include "allocator.h"

#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>

// The first piece a read makes room for.
#define PIECE 4096u

int mwinWaylandOpenPipe(mwinWaylandPipe* pipe)
{
    *pipe = (mwinWaylandPipe){.fd = -1};
    int fds[2];
    if (pipe2(fds, O_CLOEXEC) != 0)
    {
        return -1;
    }
    if (fcntl(fds[0], F_SETFL, O_NONBLOCK) != 0)
    {
        (void)close(fds[0]);
        (void)close(fds[1]);
        return -1;
    }
    pipe->fd = fds[0];
    return fds[1];
}

// Room for another piece: false when the bytes pass the limit or the
// allocator has none, with the outcome.
static bool Grow(mwinWaylandPipe* pipe, const mwinContext* context, uint32_t limit, int* outcomeOut)
{
    if (pipe->length > limit)
    {
        *outcomeOut = mwin_outcomeTooLarge;
        return false;
    }
    // One byte past the limit tells text too large from text that fits;
    // the sums are 64-bit, and a capacity the pipe cannot count fails.
    uint64_t wanted = pipe->capacity == 0 ? PIECE : (uint64_t)pipe->capacity * 2;
    uint64_t most = (uint64_t)limit + 1;
    wanted = wanted > most ? most : wanted;
    uint32_t capacity = (uint32_t)wanted;
    char* grown = wanted > UINT32_MAX ? nullptr : mwinAllocate(&context->allocator, capacity, 1);
    if (grown == nullptr)
    {
        *outcomeOut = mwin_outcomeFailed;
        return false;
    }
    if (pipe->bytes != nullptr)
    {
        memcpy(grown, pipe->bytes, pipe->length);
        mwinRelease(&context->allocator, pipe->bytes, pipe->capacity, 1);
    }
    pipe->bytes = grown;
    pipe->capacity = capacity;
    return true;
}

int mwinWaylandReadPipe(mwinWaylandPipe* pipe, const mwinContext* context, uint32_t limit)
{
    int outcome = mwin_outcomeDone;
    for (;;)
    {
        if (pipe->length == pipe->capacity && !Grow(pipe, context, limit, &outcome))
        {
            return outcome;
        }
        ssize_t got = read(pipe->fd, pipe->bytes + pipe->length, pipe->capacity - pipe->length);
        if (got == 0)
        {
            return mwin_outcomeDone;
        }
        if (got < 0)
        {
            return errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR ? -1
                                                                             : mwin_outcomeFailed;
        }
        pipe->length += (uint32_t)got;
    }
}

void mwinWaylandClosePipe(mwinWaylandPipe* pipe, const mwinContext* context)
{
    if (pipe->fd >= 0)
    {
        (void)close(pipe->fd);
    }
    if (pipe->bytes != nullptr)
    {
        mwinRelease(&context->allocator, pipe->bytes, pipe->capacity, 1);
    }
    *pipe = (mwinWaylandPipe){.fd = -1};
}
