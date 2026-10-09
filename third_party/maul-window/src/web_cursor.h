// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Cursors over canvases: the CSS cursor property, a shape's keyword,
// `none` for a hidden cursor, or for a cursor made from images
// (mwin-0027) PNG data URLs the page makes once per cursor, in an
// image-set() that gives each image its scale where the browser takes
// one in a cursor, else the first image alone.

#ifndef MAUL_WINDOW_SRC_WEB_CURSOR_H
#define MAUL_WINDOW_SRC_WEB_CURSOR_H

#include "web.h"

// Sets the cursor the window's mode, shape or image call for.
void mwinWebApplyCursor(mwinWebPlatform* platform, uint32_t slot);

// Carries out a cursor shape or image request: its outcome.
int mwinWebSetCursorShape(mwinWebPlatform* platform, uint32_t slot, mwinCursorShape shape);
int mwinWebSetCursorImage(mwinWebPlatform* platform, uint32_t slot, mwinCursorId cursor);

// The backend's releaseCursor: windows showing the cursor in a slot
// show the default shape, and the page forgets its value.
void mwinWebReleaseCursor(mwinContext* context, uint32_t slot);

#endif // MAUL_WINDOW_SRC_WEB_CURSOR_H
