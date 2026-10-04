// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Appending to debug buffers (mnav-0010), for every module that draws;
// it uses the base alone.

#ifndef MAUL_NAV_SRC_DRAW_H
#define MAUL_NAV_SRC_DRAW_H

#include "maul-nav/base.h"
#include "maul-nav/draw.h"

#include <stdbool.h>
#include <stdint.h>

// Whether a buffer is usable: counts within 0 and their capacities,
// arrays present where a capacity asks for them, an origin finite.
bool mnavGoodBuffer(const mnavDebugBuffer* buffer);

// Appends a vertex and returns its index.
uint32_t mnavDrawVertex(mnavDebugBuffer* buffer, mnavPos3 p, mnavDebugKind kind, uint16_t value);

// Appends a line, and a triangle, with vertices of their own.
void mnavDrawLine(mnavDebugBuffer* buffer, mnavPos3 a, mnavPos3 b, mnavDebugKind kind,
                  uint16_t value);
void mnavDrawTriangle(mnavDebugBuffer* buffer, mnavPos3 a, mnavPos3 b, mnavPos3 c,
                      mnavDebugKind kind, uint16_t value);

// mnav_errorCapacity when any count went past its capacity.
mnavResult mnavDrawResult(const mnavDebugBuffer* buffer);

#endif // MAUL_NAV_SRC_DRAW_H
