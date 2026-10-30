// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// MUI_SIZE_OUTLINE marks a small helper a header defines inline and many
// sources call. A build optimized for size (-Os, -Oz) keeps it out of
// line, a copy a source rather than one a call, for the size budget
// (docs/internals/overview.md); any other build inlines it as before.

#ifndef MAUL_UI_SRC_OUTLINE_H
#define MAUL_UI_SRC_OUTLINE_H

#if defined(__OPTIMIZE_SIZE__)
#define MUI_SIZE_OUTLINE __attribute__((noinline))
#else
#define MUI_SIZE_OUTLINE
#endif

#endif
