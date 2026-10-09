// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// COLR version 1 composite modes (record mui-0006), as W3C Compositing
// and Blending Level 1 defines them, on premultiplied surfaces: the
// twelve Porter-Duff operators, plus, and the fifteen blend modes, each
// blended and then composited source over.

#ifndef MAUL_UI_SRC_COLR_COMPOSITE_H
#define MAUL_UI_SRC_COLR_COMPOSITE_H

#include <stddef.h>
#include <stdint.h>

// Composites a source surface onto a backdrop in place, four
// premultiplied floats a pixel, by a mode numbered as COLR's
// CompositeMode is (0 clear to 27 luminosity); a number past them is
// source over.
void muiComposite(uint32_t mode, const float* source, float* backdrop, size_t count);

#endif // MAUL_UI_SRC_COLR_COMPOSITE_H
