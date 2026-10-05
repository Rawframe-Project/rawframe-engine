// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Android's key codes and the Linux input codes a generic keyboard sends
// for them (src/generated/android_keys.c), for keys that come with no
// scan code: the system's own, the on-screen keyboard's, injected ones.

#ifndef MAUL_WINDOW_SRC_ANDROID_KEYS_H
#define MAUL_WINDOW_SRC_ANDROID_KEYS_H

#include <stddef.h>
#include <stdint.h>

// The input code of an Android key code (AKEYCODE_*), 0 for one without.
uint32_t mwinAndroidEvdevOf(int32_t keyCode);

#endif // MAUL_WINDOW_SRC_ANDROID_KEYS_H
