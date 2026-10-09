// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Colors as transitions move them: premultiplied Oklab, as CSS Color 4
// interpolates colors where no legacy result is owed (record mui-0004).

#ifndef MAUL_UI_SRC_COLOR_H
#define MAUL_UI_SRC_COLOR_H

#include "maul-ui/draw.h"
#include "maul-ui/visual.h"

// A color's Oklab lightness, a and b, each times its alpha, and its alpha.
void muiColorToChannels(muiColor color, float channelsOut[4]);

// The color of four channels, held to the sRGB gamut and alpha from 0 to
// 1; a color with no alpha is clear black.
muiColor muiColorFromChannels(const float channels[4]);

// A color's red, green and blue in linear light.
void muiColorToLinearRgb(muiColor color, double rgbOut[3]);

// A color in linear light, its alpha multiplied by opacity and its red,
// green and blue premultiplied by that alpha, as the draw list carries it,
// from its linear red, green and blue.
muiLinearColor muiPremultiply(const double rgb[3], float alpha, float opacity);

// A channel in linear light, from 0 to 1, encoded by sRGB's transfer
// function.
float muiEncodeSrgb(float linear);

#endif // MAUL_UI_SRC_COLOR_H
