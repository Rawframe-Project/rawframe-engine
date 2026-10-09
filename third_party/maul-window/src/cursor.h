// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Cursors made from images (mwin-0027): the context's slots, and the
// image a window takes for its scale.

#ifndef MAUL_WINDOW_SRC_CURSOR_H
#define MAUL_WINDOW_SRC_CURSOR_H

#include "core.h"
#include "icon.h"

// A cursor slot: free (images NULL) or live. The backend keeps what it
// made from each image in native, as an object or as an id, NULL or 0
// where it made nothing.
typedef struct mwinCursor
{
    uint32_t generation;
    mwinIconCopy* images;
    uint32_t hotspotX;
    uint32_t hotspotY;
    void* native[MWIN_CURSOR_IMAGES];
    uint32_t nativeId[MWIN_CURSOR_IMAGES];
} mwinCursor;

// The cursor a live id names, or NULL.
mwinCursor* mwinFindCursor(const mwinContext* context, mwinCursorId cursor);

// The image a window of a scale takes: the smallest at least the first
// image's width times the scale, else the largest.
uint32_t mwinCursorImageFor(const mwinCursor* cursor, float scale);

// The hotspot of an image, scaled from the first image's.
void mwinCursorHotspotOf(const mwinCursor* cursor, uint32_t image, uint32_t* xOut, uint32_t* yOut);

// Destroys every live cursor, before the backend stops.
void mwinReleaseCursors(mwinContext* context);

#endif // MAUL_WINDOW_SRC_CURSOR_H
