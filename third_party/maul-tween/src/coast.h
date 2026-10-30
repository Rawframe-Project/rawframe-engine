// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// A transition's decay on one channel: a landing by
// the channel's own rate, a bounded decay handing over to the bounce at
// the channel's bound (its end value), or the decay alone. The start and
// the bound in the logarithm for a scale.

#ifndef MAUL_TWEEN_SRC_COAST_H
#define MAUL_TWEEN_SRC_COAST_H

#include "context_internal.h"

#include "maul-tween/spring.h"

// The bounded decay a channel that hands over runs: its bound on the
// side it moves to, or on both sides for one starting past it, as the
// bounded decay chose it from the def's bounds.
mtwnBoundedDecay mtwnCoastBounds(const mtwnPlayerSlot* slot, uint8_t i, double bound,
                                 double velocity);

// The channel's position and velocity seconds after the start.
void mtwnCoastAt(const mtwnPlayerSlot* slot, uint8_t i, double start, double bound, double velocity,
                 double seconds, double* positionOut, double* velocityOut);

#endif
