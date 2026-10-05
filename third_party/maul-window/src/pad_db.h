// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Gamepad mappings, compiled from SDL_GameControllerDB (mwin-0009) into
// src/generated/ by tools/gen_gamepad_db.py: for each device by bus,
// vendor, product and version, where each control of the standard
// location model comes from among the device's numbered buttons, axes
// and hats, as SDL numbers them.

#ifndef MAUL_WINDOW_SRC_PAD_DB_H
#define MAUL_WINDOW_SRC_PAD_DB_H

#include <stddef.h>
#include <stdint.h>

// Where a control comes from: its kind in the top two bits, then a
// button's number; an axis's number, which half of it (1 the positive,
// 2 the negative, 0 all), and whether it is inverted; or a hat's number
// and the direction's bit (1 up, 2 right, 4 down, 8 left). 0 for none.
typedef uint16_t mwinPadSource;

enum
{
    mwin_padSourceButton = 1,
    mwin_padSourceAxis = 2,
    mwin_padSourceHat = 3,
};

static inline int mwinPadSourceKind(mwinPadSource source)
{
    return source >> 14;
}

static inline uint8_t mwinPadSourceIndex(mwinPadSource source)
{
    return mwinPadSourceKind(source) == mwin_padSourceHat ? (uint8_t)((source >> 4) & 3u)
                                                          : (uint8_t)(source & 0xFFu);
}

// An axis's half (0, 1 or 2) and inversion.
static inline int mwinPadSourceHalf(mwinPadSource source)
{
    return (source >> 8) & 3;
}

static inline bool mwinPadSourceInverted(mwinPadSource source)
{
    return (source & 0x0400u) != 0;
}

// A hat's direction bit.
static inline uint8_t mwinPadSourceMask(mwinPadSource source)
{
    return (uint8_t)(source & 0x0Fu);
}

// The controls a mapping gives sources for: the mwinGamepadButton values,
// then the mwinGamepadAxis values after them.
#define MWIN_PAD_CONTROLS 21

// Halves of the sticks driven by buttons or hat directions: the negative
// then the positive half of each stick axis in mwinGamepadAxis order.
#define MWIN_PAD_HALVES 8

typedef struct mwinPadMapping
{
    mwinPadSource sources[MWIN_PAD_CONTROLS];
    // The row of the halves table plus 1, or 0 for none.
    uint8_t halves;
} mwinPadMapping;

typedef struct mwinPadEntry
{
    uint16_t bus;
    uint16_t vendor;
    uint16_t product;
    uint16_t version;
    uint16_t mapping;
} mwinPadEntry;

typedef struct mwinPadDatabase
{
    // Sorted by bus, vendor, product and version.
    const mwinPadEntry* entries;
    size_t entryCount;
    const mwinPadMapping* mappings;
    const mwinPadSource (*halves)[MWIN_PAD_HALVES];
} mwinPadDatabase;

// The mapping of a device: the exact version's, else another version's;
// NULL when the database has none.
const mwinPadMapping* mwinFindPadMapping(const mwinPadDatabase* database, uint16_t bus,
                                         uint16_t vendor, uint16_t product, uint16_t version);

// The Linux mappings (evdev).
extern const mwinPadDatabase mwinLinuxPadDatabase;

// The Windows mappings (DirectInput's numbering).
extern const mwinPadDatabase mwinWindowsPadDatabase;

#endif // MAUL_WINDOW_SRC_PAD_DB_H
