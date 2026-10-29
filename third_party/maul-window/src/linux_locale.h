// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The preferred locales on Linux, from the environment as glibc reads
// it for messages: the LANGUAGE list in order,
// unless the messages locale is C, then the messages locale, the first
// of LC_ALL, LC_MESSAGES and LANG that is set.

#ifndef MAUL_WINDOW_SRC_LINUX_LOCALE_H
#define MAUL_WINDOW_SRC_LINUX_LOCALE_H

#include <stdbool.h>
#include <stddef.h>

// The most of the preferred locales kept, in bytes.
#define MWIN_LINUX_LOCALE_BYTES 256

// The locales LANGUAGE and the messages locale name (either may be
// NULL), as BCP 47 tags with commas between them, without repeats; the
// tags that do not fit capacity are left out. Returns the length.
size_t mwinLinuxLocalesOf(const char* language, const char* messages, char* out, size_t capacity);

// The same, from the environment.
size_t mwinLinuxLocales(char* out, size_t capacity);

// Adds the tags of names separated by colons, namesLength bytes, to the
// length bytes of tags in out, as mwinLinuxLocalesOf does: names that
// are no language's, repeats and tags that do not fit are left out.
// Returns the new length.
size_t mwinLinuxAddLocales(const char* names, size_t namesLength, char* out, size_t length,
                           size_t capacity);

// The tags of KDE Plasma's plasma-localerc, length bytes of it: its
// [Translations] LANGUAGE, names separated by colons, the last one where
// there are several. False for a text with a NUL in it, which is no
// configuration; *lengthOut is 0 where the key is missing or no name in
// it is a language's.
bool mwinLinuxPlasmaLocales(const char* text, size_t length, char* out, size_t capacity,
                            size_t* lengthOut);

#endif // MAUL_WINDOW_SRC_LINUX_LOCALE_H
