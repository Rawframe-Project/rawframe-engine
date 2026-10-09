// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The chords each platform keeps (mwin-0031), as tables of rules: a key,
// or a run of keys, or any key but the modifier keys, with the
// modifiers it needs and those it allows besides; the first rule that
// matches answers, and a chord no rule matches is delivered. The tables
// are data, compiled on every host so that one test checks them all.

#ifndef MAUL_WINDOW_SRC_KEY_REACH_H
#define MAUL_WINDOW_SRC_KEY_REACH_H

#include "maul-window/input.h"

typedef struct mwinKeyRule
{
    // The first and last key of the run; first 0 for any key but the
    // modifier keys.
    uint8_t first;
    uint8_t last;
    // Shift, Control, Alt and Meta: those the chord needs, and those it
    // may hold besides.
    uint8_t needed;
    uint8_t allowed;
    mwinKeyReach reach;
} mwinKeyRule;

typedef struct mwinKeyRules
{
    const mwinKeyRule* rules;
    uint32_t count;
} mwinKeyRules;

extern const mwinKeyRules mwinKeyRulesWindows;
extern const mwinKeyRules mwinKeyRulesMacos;
extern const mwinKeyRules mwinKeyRulesIos;
extern const mwinKeyRules mwinKeyRulesAndroid;
// X11 and Wayland desktops.
extern const mwinKeyRules mwinKeyRulesLinux;
// The web in a Chromium-based browser, and in the others.
extern const mwinKeyRules mwinKeyRulesChromium;
extern const mwinKeyRules mwinKeyRulesBrowser;

// What a table answers for a chord: a key code mwinGetKeyReach accepted,
// and any modifiers, the lock bits ignored.
mwinKeyReach mwinFindKeyReach(const mwinKeyRules* rules, mwinKeyCode code, mwinModifiers modifiers);

// The backends' answers, each its platform's table; the context is
// unused.
mwinKeyReach mwinWindowsKeyReach(const mwinContext* context, mwinKeyCode code,
                                 mwinModifiers modifiers);
mwinKeyReach mwinMacosKeyReach(const mwinContext* context, mwinKeyCode code,
                               mwinModifiers modifiers);
mwinKeyReach mwinIosKeyReach(const mwinContext* context, mwinKeyCode code, mwinModifiers modifiers);
mwinKeyReach mwinAndroidKeyReach(const mwinContext* context, mwinKeyCode code,
                                 mwinModifiers modifiers);
mwinKeyReach mwinLinuxKeyReach(const mwinContext* context, mwinKeyCode code,
                               mwinModifiers modifiers);

#endif // MAUL_WINDOW_SRC_KEY_REACH_H
