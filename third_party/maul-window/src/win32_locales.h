// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Windows' preferred languages handed to the context (win32_system.c),
// with no platform type, so that tests run it on any platform.

#ifndef MAUL_WINDOW_SRC_WIN32_LOCALES_H
#define MAUL_WINDOW_SRC_WIN32_LOCALES_H

#include "core.h"

#include <stddef.h>
#include <stdint.h>

// Hands the context a list of languages as Windows names them, length
// UTF-16 units with commas between them, converted in text, of the
// localeBytes limit + 1 bytes: a first language a byte past the limit
// then reaches the context, which leaves it out. Left alone where the
// units do not fit even that.
void mwinWin32TakeLocales(mwinContext* context, const uint16_t* units, size_t length, char* text,
                          uint64_t nowNs);

#endif // MAUL_WINDOW_SRC_WIN32_LOCALES_H
