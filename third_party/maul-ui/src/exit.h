// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Exit transitions (record mui-0007), within the library.

#ifndef MAUL_UI_SRC_EXIT_H
#define MAUL_UI_SRC_EXIT_H

#include "context.h"

#include <stdint.h>

// Reports the exits under the root at slot root that no transition runs
// in any more, after transitions advance.
void muiExitAdvance(muiContext* context, uint32_t root);

#endif // MAUL_UI_SRC_EXIT_H
