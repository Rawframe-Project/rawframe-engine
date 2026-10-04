// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Internal invariants only (conventions section 8): states that cannot
// happen unless the library itself is wrong. Caller input is refused
// with a status, never checked here. Debug and test builds stop at a
// broken invariant, where a debugger shows it; release builds (NDEBUG)
// compile the check away. The library prints nothing.

#ifndef MAUL_AUDIO_SRC_INVARIANT_H
#define MAUL_AUDIO_SRC_INVARIANT_H

#if defined(NDEBUG)
// sizeof keeps the operands used without evaluating them.
#define MAUD_ASSERT(cond) ((void)sizeof(cond))
#else
#define MAUD_ASSERT(cond) ((cond) ? (void)0 : __builtin_trap())
#endif

#endif // MAUL_AUDIO_SRC_INVARIANT_H
