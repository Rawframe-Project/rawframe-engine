// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The walkable filters: which spans of a heightfield an agent can
// stand on.

#ifndef MAUL_NAV_SRC_FILTER_H
#define MAUL_NAV_SRC_FILTER_H

#include "heightfield.h"

#include <stdint.h>

// Gives a non-walkable span the area of the walkable span directly below
// it when its top is at most step cells higher; then makes a walkable span
// unwalkable when a neighbor drops more than step cells into space at
// least height cells tall, when the neighbors it can reach differ by more
// than step cells, or when less than height cells are free above it. A
// cell outside the field counts as a drop.
void mnavFilterWalkable(mnavHeightfield* heightfield, int32_t height, int32_t step);

#endif // MAUL_NAV_SRC_FILTER_H
