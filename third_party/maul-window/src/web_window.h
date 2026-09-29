// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Web windows: a window is a canvas, one the page names or one the
// backend makes and appends to the body. Its logical size is the
// canvas's CSS size and its pixels the device pixels of that box, which
// the drawing buffer follows. The page has no place for a window, no
// maximizing or minimizing, and no size limits; fullscreen is the
// Fullscreen API's, which the browser grants only after a user's
// gesture, so a mode request may be denied.

#ifndef MAUL_WINDOW_SRC_WEB_WINDOW_H
#define MAUL_WINDOW_SRC_WEB_WINDOW_H

#include "web.h"

// The backend's createWindow, destroyWindow and submit.
void mwinWebCreateWindow(mwinContext* context, uint32_t slot);
void mwinWebDestroyWindow(mwinContext* context, uint32_t slot);
void mwinWebSubmit(mwinContext* context, uint32_t slot, uint32_t request);

// Handles a record about a window of the page's.
void mwinWebHandleWindowRecord(mwinWebPlatform* platform, const mwinWebRecord* record);

#endif // MAUL_WINDOW_SRC_WEB_WINDOW_H
