// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The scale desktops set on X11 through Xft.dpi in the root window's
// resources (mwin-0006): the number after "Xft.dpi:" at the start of a
// line, past spaces and tabs, with one decimal point at most, over the
// 96 pixels per inch of a scale of 1. A scale under a quarter or over 8,
// or no Xft.dpi at all, is 1.

#ifndef MAUL_WINDOW_SRC_X11_RESOURCES_H
#define MAUL_WINDOW_SRC_X11_RESOURCES_H

#include <stddef.h>

// The scale the resource text sets, 1 where it sets none.
float mwinX11ScaleOfResources(const char* text, size_t length);

#endif // MAUL_WINDOW_SRC_X11_RESOURCES_H
