// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// A spring as an easing, for src/easing.c: the
// sample lives with the spring's layout in src/spring.c.

#ifndef MAUL_TWEEN_SRC_SPRING_EASING_H
#define MAUL_TWEEN_SRC_SPRING_EASING_H

#include "maul-tween/easing.h"

// The spring easing made by mtwnMakeSpringEasing at progress p in [0, 1].
double mtwnSampleSpringEasing(const mtwnEasing* easing, double p);

// Whether a spring easing's fields are in the ranges its maker gives them,
// which keep its sample finite on [0, 1].
bool mtwnValidSpringEasing(const mtwnEasing* easing);

#endif // MAUL_TWEEN_SRC_SPRING_EASING_H
