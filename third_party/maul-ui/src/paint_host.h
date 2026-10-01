// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Painting host content (record mui-0005): the host's paint function adds
// glyph runs through a sink, at positions relative to the node's content
// box.

#ifndef MAUL_UI_SRC_PAINT_HOST_H
#define MAUL_UI_SRC_PAINT_HOST_H

#include "draw_store.h"
#include "paint.h"

#include <stdint.h>

// Calls the host's paint function for a node whose content is the
// host's, painted at the origin, clip and opacity in state.
void muiPaintHostContent(muiPainter* painter, uint32_t slot, const muiPaintState* state);

#endif // MAUL_UI_SRC_PAINT_HOST_H
