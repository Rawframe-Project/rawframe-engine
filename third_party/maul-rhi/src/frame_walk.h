// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The walk of a submitted frame (mrhi-0013): every pass's commands
// checked as a real driver would read them. The test driver traps on a
// fault; the validation layer counts them (mrhi-0025).

#ifndef MAUL_RHI_SRC_FRAME_WALK_H
#define MAUL_RHI_SRC_FRAME_WALK_H

#include "driver.h"

#include "maul-rhi/test.h"

// Whether a handle names an object of the frame's device.
typedef bool (*mrhiHandleCheck)(const void* handles, uint64_t handle);

// Walks a frame, its handles checked by isHandle with handles, and
// reports it into log, all but the frames counted, unless log is NULL:
// the faults found, records no driver could translate. The walk stops
// where a fault leaves later reads unsafe.
uint64_t mrhiWalkFrame(const mrhiDriverFrame* frame, mrhiHandleCheck isHandle, const void* handles,
                       mrhiTestFrameLog* log);

#endif // MAUL_RHI_SRC_FRAME_WALK_H
