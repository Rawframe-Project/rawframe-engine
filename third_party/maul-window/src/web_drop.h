// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Web drag and drop: a canvas's dragenter, dragover, dragleave and drop
// events, for drags that carry files or plain text; the page's default,
// which would open what was dropped, is kept from them. A drag over
// that has not moved is not reported again. Files come as their names,
// the only thing a page learns of them, and a name longer than a
// window's text storage is left out.

#ifndef MAUL_WINDOW_SRC_WEB_DROP_H
#define MAUL_WINDOW_SRC_WEB_DROP_H

#include "web.h"

// Listens for drags over a window's canvas.
void mwinWebWatchDrops(const mwinContext* context, uint32_t slot);

// Posts a drag's record, or gathers and delivers a drop; a drop whose
// window went is let go.
void mwinWebHandleDropRecord(mwinWebPlatform* platform, const mwinWebRecord* record);

#endif // MAUL_WINDOW_SRC_WEB_DROP_H
