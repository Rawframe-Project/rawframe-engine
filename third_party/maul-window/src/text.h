// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// A window's text storage: the text of waiting records, and the segments
// of compositions, in one circular buffer of bytes (see mwinTextRing).

#ifndef MAUL_WINDOW_SRC_TEXT_H
#define MAUL_WINDOW_SRC_TEXT_H

#include "core.h"

// Reserves size bytes at an offset that is a multiple of align, a power of
// two dividing the buffer's own alignment; NULL when they do not fit.
void* mwinReserveText(mwinTextRing* ring, uint32_t size, uint32_t align);

// Marks the bytes up to end, which a drained record used, for reclaiming
// at the next pump; start is where that record's bytes begin.
void mwinDrainText(mwinTextRing* ring, const void* start, const void* end);

// Reclaims what was drained; called when a pump begins.
void mwinReclaimText(mwinTextRing* ring);

// Lets everything go at the next pump, as when the window is destroyed.
void mwinDropText(mwinTextRing* ring);

#endif // MAUL_WINDOW_SRC_TEXT_H
