// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The page's side of the web backend, in JavaScript: each function takes
// the context as the key of that context's state on the page.

#ifndef MAUL_WINDOW_SRC_WEB_PAGE_H
#define MAUL_WINDOW_SRC_WEB_PAGE_H

#include "web.h"

// Whether there is a page (not a worker, not Node).
bool mwinWebHasPage(void);

// What the page calls, with 0 when it goes away and 1 when it comes
// back, from inside the browser's event.
typedef void (*mwinWebLifecycle)(mwinContext* context, int running);

// Sets up and takes down the context's state and the page's listeners.
void mwinWebAttach(mwinContext* context, mwinWebLifecycle lifecycle);
void mwinWebDetach(const mwinContext* context);

// Takes the next record the page reported: false when there is none.
bool mwinWebNext(const mwinContext* context, mwinWebRecord* out);

// Finds the canvas a selector names, or makes one of a CSS size, and
// watches it. Writes its selector and its box (CSS width and height,
// pixel width and height); returns the selector's length, or -1 when
// the selector names no canvas or the selector does not fit.
int mwinWebOpenCanvas(const mwinContext* context, uint32_t slot, const char* selector,
                      size_t length, float width, float height, bool visible, char* out,
                      uint32_t capacity, float* box);
// Stops watching a canvas, and removes it when the backend made it.
void mwinWebCloseCanvas(const mwinContext* context, uint32_t slot);

void mwinWebSetTitle(const mwinContext* context, uint32_t slot, const char* title, size_t length);
void mwinWebSetSize(const mwinContext* context, uint32_t slot, float width, float height);
void mwinWebSetVisible(const mwinContext* context, uint32_t slot, bool visible);
void mwinWebSetOpacity(const mwinContext* context, uint32_t slot, float opacity);
// Focuses a canvas: false when the page did not let it.
bool mwinWebFocus(const mwinContext* context, uint32_t slot);
// Asks for fullscreen or out of it: 1 when the canvas is so already, 0
// when a record will say, -1 when the browser has no fullscreen.
int mwinWebSetFullscreen(const mwinContext* context, uint32_t slot, bool fullscreen);

// devicePixelRatio, and the screen's CSS size and available size.
float mwinWebScale(void);
void mwinWebScreen(float* out);

// Looks at devicePixelRatio, reporting a change the browser did not.
void mwinWebCheckScale(const mwinContext* context);

// The preferred color scheme (an mwinTheme) and reduced motion.
int mwinWebTheme(void);
bool mwinWebReducedMotion(void);
// navigator.languages with commas, in UTF-8: the bytes it needs.
uint32_t mwinWebLocales(char* out, uint32_t capacity);

#endif // MAUL_WINDOW_SRC_WEB_PAGE_H
