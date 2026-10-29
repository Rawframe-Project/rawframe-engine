// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Internal invariants only: states that cannot
// happen unless the library itself is wrong. Caller input is refused
// with a status, never checked here. Debug and test builds stop at a
// broken invariant, where a debugger shows it; release builds (NDEBUG)
// compile the check away. The library prints nothing.

#ifndef MAUL_NAV_SRC_INVARIANT_H
#define MAUL_NAV_SRC_INVARIANT_H

#if defined(NDEBUG)
// sizeof keeps the operands used without evaluating them.
#define MNAV_ASSERT(cond) ((void)sizeof(cond))
#define MNAV_INVARIANTS   0
#else
#define MNAV_ASSERT(cond) ((cond) ? (void)0 : __builtin_trap())
#define MNAV_INVARIANTS   1
#endif

// A check of a whole structure, a loop of MNAV_ASSERTs, is called as
// `if (MNAV_INVARIANTS) { Check...(...); }`, so that release builds drop
// the loop with the assertions while it still compiles there.
//
// A value that only assertions read is marked read after the last of
// them, as `(void)value;`. Without it, the static analyzer takes the
// value's store for a dead one in release builds, where the assertions
// compile out; clang-tidy runs on both builds.

#endif // MAUL_NAV_SRC_INVARIANT_H
