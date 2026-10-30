// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The stagger's random draw, for its tests.

#ifndef MAUL_TWEEN_SRC_STAGGER_INTERNAL_H
#define MAUL_TWEEN_SRC_STAGGER_INTERNAL_H

#include <stdint.h>

// A place in [0, range) from a seed's stream, each equally likely
// (Lemire's multiply with rejection); range at least 1. Advances state.
uint32_t mtwnUniformBelow(uint64_t* state, uint32_t range);

#endif // MAUL_TWEEN_SRC_STAGGER_INTERNAL_H
