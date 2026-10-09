// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Images scaled for colour bitmap glyphs (record mui-0006): premultiplied
// pixels in linear light, averaged over each output pixel's area when
// shrinking and sampled bilinearly when growing, placed to a fraction of
// a pixel.

#ifndef MAUL_UI_SRC_IMAGE_SCALE_H
#define MAUL_UI_SRC_IMAGE_SCALE_H

#include <stdint.h>

// A source image: width * height pixels of four floats, rows from the
// top.
typedef struct muiScaleSource
{
    const float* pixels;
    uint32_t width;
    uint32_t height;
} muiScaleSource;

// Scales a source by factor into an output of width * height pixels of
// four floats, rows from the top, the source's top left landing at x, y
// in the output's pixels (y down); the output's pixels the source does
// not reach are clear.
void muiScaleImage(const muiScaleSource* source, double factor, double x, double y, float* out,
                   uint32_t width, uint32_t height);

#endif // MAUL_UI_SRC_IMAGE_SCALE_H
