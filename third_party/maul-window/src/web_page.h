// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The page's side of the web backend, in JavaScript: each function takes
// the context as the key of that context's state on the page.

#ifndef MAUL_WINDOW_SRC_WEB_PAGE_H
#define MAUL_WINDOW_SRC_WEB_PAGE_H

#include "web.h"
#include "web_js.h"

// Whether there is a page (not a worker, not Node).
bool mwinWebHasPage(void);

// What the page calls, with 0 when it goes away and 1 when it comes
// back, from inside the browser's event.
typedef void (*mwinWebLifecycle)(mwinContext* context, int running);

// Sets up and takes down the context's state and the page's listeners.
MWIN_WEB_IMPORT(mwinWebAttach) void mwinWebAttach(mwinContext* context, mwinWebLifecycle lifecycle);
MWIN_WEB_IMPORT(mwinWebDetach) void mwinWebDetach(const mwinContext* context);

// Takes the next record the page reported: false when there is none.
MWIN_WEB_IMPORT(mwinWebNext) bool mwinWebNext(const mwinContext* context, mwinWebRecord* out);

// Finds the canvas a selector names, or makes one of a CSS size, and
// watches it. Writes its selector and its box (CSS width and height,
// pixel width and height); returns the selector's length, or -1 when
// the selector names no canvas or the selector does not fit.
MWIN_WEB_IMPORT(mwinWebOpenCanvas)
int mwinWebOpenCanvas(const mwinContext* context, uint32_t slot, const char* selector,
                      size_t length, float width, float height, bool visible, char* out,
                      uint32_t capacity, float* box);
// Stops watching a canvas, and removes it when the backend made it.
MWIN_WEB_IMPORT(mwinWebCloseCanvas)
void mwinWebCloseCanvas(const mwinContext* context, uint32_t slot);

MWIN_WEB_IMPORT(mwinWebSetTitle)
void mwinWebSetTitle(const mwinContext* context, uint32_t slot, const char* title, size_t length);
MWIN_WEB_IMPORT(mwinWebSetSize)
void mwinWebSetSize(const mwinContext* context, uint32_t slot, float width, float height);
MWIN_WEB_IMPORT(mwinWebSetVisible)
void mwinWebSetVisible(const mwinContext* context, uint32_t slot, bool visible);
MWIN_WEB_IMPORT(mwinWebSetOpacity)
void mwinWebSetOpacity(const mwinContext* context, uint32_t slot, float opacity);
// Focuses a canvas: false when the page did not let it.
MWIN_WEB_IMPORT(mwinWebFocus) bool mwinWebFocus(const mwinContext* context, uint32_t slot);
// Asks for fullscreen or out of it: 1 when the canvas is so already, 0
// when a record will say, -1 when the browser has no fullscreen.
MWIN_WEB_IMPORT(mwinWebSetFullscreen)
int mwinWebSetFullscreen(const mwinContext* context, uint32_t slot, bool fullscreen);

// devicePixelRatio, and the screen's CSS size and available size.
MWIN_WEB_IMPORT(mwinWebScale) float mwinWebScale(void);
MWIN_WEB_IMPORT(mwinWebScreen) void mwinWebScreen(float* out);

// Looks at devicePixelRatio, reporting a change the browser did not.
MWIN_WEB_IMPORT(mwinWebCheckScale) void mwinWebCheckScale(const mwinContext* context);

// Whether the screen and browser show HDR (dynamic-range: high).
MWIN_WEB_IMPORT(mwinWebHighDynamicRange) bool mwinWebHighDynamicRange(void);

// The preferred color scheme (an mwinTheme) and reduced motion.
MWIN_WEB_IMPORT(mwinWebTheme) int mwinWebTheme(void);
MWIN_WEB_IMPORT(mwinWebReducedMotion) bool mwinWebReducedMotion(void);
// navigator.languages with commas, in UTF-8: the bytes it needs.
MWIN_WEB_IMPORT(mwinWebLocales) uint32_t mwinWebLocales(char* out, uint32_t capacity);

#endif // MAUL_WINDOW_SRC_WEB_PAGE_H
