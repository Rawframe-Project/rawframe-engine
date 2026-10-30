// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Internal invariants only (Maul conventions, Errors and assertions): states that cannot
// happen unless the library itself is wrong. Caller input is refused with a status,
// never checked here. Debug and test builds stop at a broken invariant, where a debugger
// shows it; release builds (NDEBUG) compile the check away. The library prints nothing.

#ifndef MAUL_UI_SRC_INVARIANT_H
#define MAUL_UI_SRC_INVARIANT_H

#if defined(NDEBUG)
// sizeof keeps the operands used without evaluating them; the
// conditional keeps a sizeof among them from reading as a mistake.
#define MUI_ASSERT(cond) ((void)sizeof((cond) ? 1 : 0))
#else
#define MUI_ASSERT(cond) ((cond) ? (void)0 : __builtin_trap())
#endif

// Asserts that a call the library cannot have made wrong succeeded: the
// call is made in every build, and what it returned checked where
// assertions are. A status kept in a variable only an assertion reads
// would be stored for nothing in a release build.
static inline void muiAssertCall(bool succeeded)
{
    MUI_ASSERT(succeeded);
}

#endif // MAUL_UI_SRC_INVARIANT_H
