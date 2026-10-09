// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// COLR version 1 gradients (record mui-0006): linear, radial and sweep,
// each pixel's centre taken back into font units and given its colour
// on the gradient's colour line, interpolated in premultiplied linear
// light as CPAL's "Interpolation of colors" asks.

#ifndef MAUL_UI_SRC_COLR_GRADIENT_H
#define MAUL_UI_SRC_COLR_GRADIENT_H

#include "paint_source.h"

#if MUI_COLR_PAINT

// An affine map of font units to pixels' font units: x' = xx x + xy y +
// dx, y' = yx x + yy y + dy, before the size scales it and the pen's
// offset moves it.
typedef struct muiPaintMatrix
{
    double xx;
    double xy;
    double dx;
    double yx;
    double yy;
    double dy;
} muiPaintMatrix;

// Paints a gradient over a box of pixels, four premultiplied floats a
// pixel, rows from the top, by the matrix that places font units; pixels
// it does not reach, or all of them for a gradient with no extent, are
// left clear.
muiResult muiPaintGradient(const muiPaintSource* source, const FT_COLR_Paint* paint,
                           muiPaintMatrix m, const muiPixelBox* box, float* pixels);

#endif

#endif // MAUL_UI_SRC_COLR_GRADIENT_H
