// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The desktop's languages on Linux, followed while the program runs; the
// preferred locales start from the environment (linux_locale.h):
// - GNOME's, the user's Languages (else Language) in AccountsService on
//   the system bus: the user's path from FindUserById, its properties
//   from GetAll, then their PropertiesChanged, or GetAll again on the
//   older Changed signal; signals only from the connection that gave the
//   user's path. When AccountsService's name changes owner (a restart),
//   the old one is trusted no more and the new one asked for the user.
// - KDE Plasma's, plasma-localerc's [Translations] LANGUAGE in the
//   configuration folder ($XDG_CONFIG_HOME, else ~/.config), watched
//   through inotify, read without waiting at each pump and at most once
//   a pump, and once more after the watch's queue overflowed.
// Each source's first word is its baseline. A later one that differs
// replaces the preferred locales, with the starting list where it names
// no language; mwin_eventLocaleChanged comes only when the list changes.
// A source that is not there changes nothing, and a reply or file in a
// shape not expected is left alone like any other input from outside.
// The calls are only queued (mwinBusQueue), never sent with a pump: a
// pump dispatches, and a filter run then, as for an owner that went,
// would change the state the module is in the middle of using. A
// dispatch from inside a dispatch would also wait on itself.

#ifndef MAUL_WINDOW_SRC_LINUX_LANGUAGES_H
#define MAUL_WINDOW_SRC_LINUX_LANGUAGES_H

#include "core.h"
#include "linux_bus.h"
#include "linux_locale.h"

// The longest AccountsService user path kept, in bytes.
#define MWIN_LINUX_USER_PATH_BYTES 128
// The longest unique bus name of AccountsService kept, in bytes.
#define MWIN_LINUX_OWNER_BYTES 64
// The longest configuration folder followed, in bytes.
#define MWIN_LINUX_CONFIG_PATH_BYTES 512
// The most of plasma-localerc read: a longer file is left alone.
#define MWIN_LINUX_PLASMA_BYTES 4096

// What a source last said: nothing yet, or its tags.
typedef struct mwinLinuxLanguageWord
{
    bool said;
    uint16_t length;
    char tags[MWIN_LINUX_LOCALE_BYTES];
} mwinLinuxLanguageWord;

typedef struct mwinLinuxLanguages
{
    mwinContext* context;
    mwinLinuxBus* system;
    mwinBusCall userCall;
    mwinBusCall readCall;
    bool listening;
    // The user's path, "" until known, and the unique name of the
    // connection that gave it.
    char user[MWIN_LINUX_USER_PATH_BYTES];
    char owner[MWIN_LINUX_OWNER_BYTES];
    // What the filter saw for the pump to do: forget the old owner's calls
    // and stop listening at its path (stale, "" for none), ask a new owner
    // for the user, read the user again.
    bool forget;
    bool refind;
    bool reread;
    char stale[MWIN_LINUX_USER_PATH_BYTES];
    // The starting list, and the most a list may hold.
    char start[MWIN_LINUX_LOCALE_BYTES];
    uint16_t startLength;
    uint16_t capacity;
    mwinLinuxLanguageWord gnome;
    mwinLinuxLanguageWord kde;
    // The inotify watch, -1 for none, and plasma-localerc's path.
    int watch;
    char file[MWIN_LINUX_CONFIG_PATH_BYTES + 32];
} mwinLinuxLanguages;

// Takes the starting list, asks AccountsService for the user, and
// watches plasma-localerc; whatever is not there is left out.
void mwinLanguagesStart(mwinLinuxLanguages* languages, mwinContext* context, mwinLinuxBus* system,
                        const char* start, size_t startLength, uint64_t nowNs);

// Takes the answers that came, after the system bus's pump, and reads
// plasma-localerc when it changed.
void mwinLanguagesPump(mwinLinuxLanguages* languages, uint64_t nowNs);

// Stops listening and closes the watch, before the system bus closes.
void mwinLanguagesStop(mwinLinuxLanguages* languages);

#endif // MAUL_WINDOW_SRC_LINUX_LANGUAGES_H
