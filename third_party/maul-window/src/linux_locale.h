// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The preferred locales on Linux, from the environment as glibc reads
// it for messages (window decision W9): the LANGUAGE list in order,
// unless the messages locale is C, then the messages locale, the first
// of LC_ALL, LC_MESSAGES and LANG that is set.

#ifndef MAUL_WINDOW_SRC_LINUX_LOCALE_H
#define MAUL_WINDOW_SRC_LINUX_LOCALE_H

#include <stddef.h>

// The locales LANGUAGE and the messages locale name (either may be
// NULL), as BCP 47 tags with commas between them, without repeats; the
// tags that do not fit capacity are left out. Returns the length.
size_t mwinLinuxLocalesOf(const char* language, const char* messages, char* out, size_t capacity);

// The same, from the environment.
size_t mwinLinuxLocales(char* out, size_t capacity);

#endif // MAUL_WINDOW_SRC_LINUX_LOCALE_H
