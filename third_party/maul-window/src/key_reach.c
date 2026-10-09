// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The tables of the chords each platform keeps, from what the platforms
// document and do (mwin-0031).

#include "key_reach.h"

#define SHIFT   mwin_modShift
#define CONTROL mwin_modControl
#define ALT     mwin_modAlt
#define META    mwin_modMeta
#define ALL     (SHIFT | CONTROL | ALT | META)
#define ANY     0

#define RULES(name, ...)                                                                           \
    static const mwinKeyRule s_##name[] = {__VA_ARGS__};                                           \
    const mwinKeyRules mwinKeyRules##name = {s_##name, sizeof(s_##name) / sizeof(s_##name[0])}

// One key, a run of keys, or any key but the modifier keys.
#define KEY(code, needed, allowed, reach)                                                          \
    {mwin_code##code, mwin_code##code, (needed), (allowed), mwin_keyReach##reach}
#define KEYS(first, last, needed, allowed, reach)                                                  \
    {mwin_code##first, mwin_code##last, (needed), (allowed), mwin_keyReach##reach}
#define ANY_KEY(needed, allowed, reach) {ANY, ANY, (needed), (allowed), mwin_keyReach##reach}

// The secure attention sequence and the shell's own chords never reach a
// window; Explorer's other Windows-key hotkeys and the snipping tool may
// take theirs; the system keys the backend passes to DefWindowProc
// arrive and act there too, as does the Windows key opening Start.
RULES(Windows, KEY(Delete, CONTROL | ALT, SHIFT, Never), KEY(Tab, ALT, SHIFT | CONTROL, Never),
      KEY(Escape, CONTROL, SHIFT, Never), KEY(KeyL, META, ALL, Never),
      KEY(Escape, ALT, SHIFT, Uncertain), ANY_KEY(META, ALL, Uncertain),
      KEY(PrintScreen, ANY, ALL, Uncertain), KEY(MetaLeft, ANY, ALL, Shared),
      KEY(MetaRight, ANY, ALL, Shared), KEY(F4, ALT, ANY, Shared), KEY(Space, ALT, ANY, Shared));

// The application switcher and Force Quit are the system's; Spotlight,
// the input source switch, screenshots, Mission Control and window
// cycling are the user's to change in System Settings.
RULES(Macos, KEY(Tab, META, SHIFT, Never), KEY(Escape, META | ALT, ANY, Never),
      KEY(Space, META, ALT, Uncertain), KEY(Space, CONTROL, ALT, Uncertain),
      KEYS(Digit3, Digit5, META | SHIFT, CONTROL, Uncertain),
      KEYS(ArrowRight, ArrowUp, CONTROL, ANY, Uncertain), KEY(Backquote, META, SHIFT, Uncertain),
      KEY(KeyQ, META | CONTROL, ANY, Uncertain));

// With a hardware keyboard: the app switcher and Home are the system's.
RULES(Ios, KEY(Tab, META, SHIFT, Never), KEY(KeyH, META, ANY, Never),
      KEY(Space, META, ANY, Uncertain));

// The system keys (Home, volume, media) and Back have no codes; Meta
// chords and Alt+Tab are the system's shortcuts where a device has them.
RULES(Android, ANY_KEY(META, ALL, Uncertain), KEY(Tab, ALT, SHIFT, Uncertain));

// The window manager's or compositor's shortcuts, all configurable.
RULES(Linux, ANY_KEY(META, ALL, Uncertain), KEY(Tab, ALT, SHIFT | CONTROL, Uncertain),
      KEYS(F1, F12, ALT, SHIFT | CONTROL, Uncertain), KEY(PrintScreen, ANY, ALL, Uncertain));

// Chromium dispatches no event for the chords its own UI owns. Every
// browser leaves fullscreen on Escape. The backend lets the browser act
// on Control or Meta chords without Alt and on F5, F11 and F12, which
// still arrive.
#define BROWSER_SHARED                                                                             \
    KEY(Escape, ANY, ALL, Uncertain), ANY_KEY(CONTROL, SHIFT | META, Shared),                      \
        ANY_KEY(META, SHIFT, Shared), KEY(F5, ANY, ALL, Shared), KEYS(F11, F12, ANY, ALL, Shared)

RULES(Chromium, KEY(KeyN, CONTROL, SHIFT, Never), KEY(KeyT, CONTROL, SHIFT, Never),
      KEY(KeyW, CONTROL, SHIFT, Never), KEY(Tab, CONTROL, SHIFT, Never),
      KEY(PageUp, CONTROL, SHIFT, Never), KEY(PageDown, CONTROL, SHIFT, Never),
      KEY(KeyN, META, SHIFT, Never), KEY(KeyT, META, SHIFT, Never), KEY(KeyW, META, SHIFT, Never),
      KEY(KeyQ, META, ANY, Never), BROWSER_SHARED);

RULES(Browser, BROWSER_SHARED);

mwinKeyReach mwinFindKeyReach(const mwinKeyRules* rules, mwinKeyCode code, mwinModifiers modifiers)
{
    uint8_t held = (uint8_t)(modifiers & ALL);
    bool modifier = code >= mwin_codeControlLeft && code <= mwin_codeMetaRight;
    for (uint32_t i = 0; i < rules->count; i++)
    {
        const mwinKeyRule* rule = &rules->rules[i];
        bool key = rule->first == ANY ? !modifier : code >= rule->first && code <= rule->last;
        if (key && (held & rule->needed) == rule->needed &&
            (held & (uint8_t)~(rule->needed | rule->allowed)) == 0)
        {
            return rule->reach;
        }
    }
    return mwin_keyReachDelivered;
}

#define BACKEND_REACH(name, table)                                                                 \
    mwinKeyReach name(const mwinContext* context, mwinKeyCode code, mwinModifiers modifiers)       \
    {                                                                                              \
        (void)context;                                                                             \
        return mwinFindKeyReach(&(table), code, modifiers);                                        \
    }

BACKEND_REACH(mwinWindowsKeyReach, mwinKeyRulesWindows)
BACKEND_REACH(mwinMacosKeyReach, mwinKeyRulesMacos)
BACKEND_REACH(mwinIosKeyReach, mwinKeyRulesIos)
BACKEND_REACH(mwinAndroidKeyReach, mwinKeyRulesAndroid)
BACKEND_REACH(mwinLinuxKeyReach, mwinKeyRulesLinux)
