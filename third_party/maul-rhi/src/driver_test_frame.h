// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The test driver's walk of a submitted frame (mrhi-0013): every pass's
// commands checked as a real driver would read them, trapping on any
// record a driver could not translate.

#ifndef MAUL_RHI_SRC_DRIVER_TEST_FRAME_H
#define MAUL_RHI_SRC_DRIVER_TEST_FRAME_H

#include "driver.h"

#include "maul-rhi/test.h"

// Walks a frame whose device's handles run from firstHandle to
// lastHandle, trapping on a malformed view, and reports it into log,
// all but the frames counted, unless log is NULL.
void mrhiWalkTestFrame(const mrhiDriverFrame* frame, uint64_t firstHandle, uint64_t lastHandle,
                       mrhiTestFrameLog* log);

#endif // MAUL_RHI_SRC_DRIVER_TEST_FRAME_H
