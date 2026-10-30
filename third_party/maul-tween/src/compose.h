// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Composition: an effect value onto the underlying
// value by an operation and a weight, per value kind.

#ifndef MAUL_TWEEN_SRC_COMPOSE_H
#define MAUL_TWEEN_SRC_COMPOSE_H

#include "maul-tween/base.h"
#include "maul-tween/player.h"

// A value and its velocity.
typedef struct mtwnMoving
{
    mtwnVec4 value;
    mtwnVec4 velocity;
} mtwnMoving;

// The underlying value with an effect composited on it, mixed toward by
// the weight; an integer's effect comes unrounded and the result rounds.
mtwnMoving mtwnCompose(mtwnValueKind kind, mtwnComposite composite, double weight,
                       mtwnMoving underlying, mtwnMoving effect);

#endif
