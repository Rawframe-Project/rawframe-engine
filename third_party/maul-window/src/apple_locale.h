// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The preferred languages on Apple's systems, which the macOS and iOS
// backends share. Included by Objective-C files only.

#ifndef MAUL_WINDOW_SRC_APPLE_LOCALE_H
#define MAUL_WINDOW_SRC_APPLE_LOCALE_H

#include "core.h"

// Hands the core the preferred languages, comma-separated; whole tags
// from the end left out while the list is past the limit.
void mwinAppleReadLocales(mwinContext* context, uint64_t nowNs);

#endif // MAUL_WINDOW_SRC_APPLE_LOCALE_H
