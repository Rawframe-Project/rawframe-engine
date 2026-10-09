// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The name a document's copy takes on Android (android_copy.h), from the
// name its provider gives, with no platform type, so that tests run it on
// any platform: in UTF-8, cut to whole characters within
// MWIN_ANDROID_NAME_BYTES, each '/' made '_' so that the name stays in
// its folder; "Document" for none or a name that is not one ("." and
// "..").

#ifndef MAUL_WINDOW_SRC_ANDROID_NAME_H
#define MAUL_WINDOW_SRC_ANDROID_NAME_H

#include <stddef.h>
#include <stdint.h>

// The longest name a copy keeps, in bytes.
#define MWIN_ANDROID_NAME_BYTES 255u

// Writes the name of a provider's UTF-16 units (none when null), ended by
// a NUL, into name, of MWIN_ANDROID_NAME_BYTES + 1 bytes.
void mwinAndroidNameOf(const uint16_t* units, size_t count, char* name);

#endif // MAUL_WINDOW_SRC_ANDROID_NAME_H
