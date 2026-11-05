// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// MUNI_ASSERT states an internal invariant: a state that cannot occur
// unless the library itself is wrong. Input from callers is never
// asserted; it is validated and refused with a status. The check traps
// in every build without NDEBUG (Debug, the sanitizer and fuzz builds),
// so a broken invariant stops the run where it happened instead of
// surfacing later as a wrong answer. Release builds compile it out
// without evaluating the condition.

#ifndef MAUL_UNICODE_SRC_INVARIANT_H
#define MAUL_UNICODE_SRC_INVARIANT_H

#if defined(NDEBUG)
#define MUNI_ASSERT(condition) ((void)sizeof((condition) ? 1 : 0))
#else
#define MUNI_ASSERT(condition) ((condition) ? (void)0 : __builtin_trap())
#endif

// MUNI_ALWAYS_INLINE asks that a small function be inlined into every
// caller, for a check on a call's hot path that the compiler would
// otherwise leave as a call of its own. Every compiler the family builds
// with, clang-cl included, takes the attribute.
#define MUNI_ALWAYS_INLINE inline __attribute__((always_inline))

#endif // MAUL_UNICODE_SRC_INVARIANT_H
