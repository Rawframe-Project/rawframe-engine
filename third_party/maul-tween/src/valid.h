// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// One check per kind of value struct: whether a maker could have produced
// it, or, where that would cost a sample's own price, whether its fields
// lie in the ranges that keep every sample of it free of NaN and of
// undefined behavior. A value struct's fields are the library's, but a
// host holds it and may write any bytes into it, so every public call
// taking one checks it, and so does every path taking one into a context.
// Both call these, so the two cannot drift. Each lives in its kind's
// module; a color's, a rotation's and a scale's mix's in its kind's check
// module, apart from the mix a transition links.

#ifndef MAUL_TWEEN_SRC_VALID_H
#define MAUL_TWEEN_SRC_VALID_H

#include "maul-tween/easing.h"
#include "maul-tween/interpolate.h"
#include "maul-tween/path.h"
#include "maul-tween/spring.h"

#include <stdbool.h>

// An easing's fields, those its kind reads, as its maker gives them:
// exactly where the maker computes them exactly (the normalizations of the
// curves without parameters, a power's and a back's, the CSS curves' own
// fields), and otherwise in the ranges that keep its sample finite over
// progress 0 to 1 (an elastic, a spring easing). linear()'s points are
// the host's and not read; its sampler is finite for any finite points.
bool mtwnValidEasing(const mtwnEasing* easing);

// A spring's precomputed fields derived again from its frequency and ratio,
// bit for bit.
bool mtwnValidSpring(const mtwnSpring* spring);

// A decay's rate finite and above 0.
bool mtwnValidDecay(const mtwnDecay* decay);

// A bounded decay's decay and spring valid, and its bounds as
// mtwnMakeBoundedDecay takes them.
bool mtwnValidBoundedDecay(const mtwnBoundedDecay* bounded);

// A path's own fields as mtwnMakePath and mtwnMakeTrack set them, and with
// lengths their first 0 and their total finite. The keyframes are the
// host's and not read: a sample checks the segment it reads
// (mtwnPathReadsValid).
bool mtwnValidPath(const mtwnPath* path);

// A track's fields and its path's as mtwnMakeTrack sets them.
bool mtwnValidTrack(const mtwnTrack* track);

// A color mix's fields in the ranges that keep its sample finite, not
// necessarily a maker's: a known space and its padding 0, both colors as
// mtwnMakeColorMix takes them, and each end's coordinates finite, its
// alpha its color's and an OkLCh hue within the two turns the hue
// fix-ups reach. Its coordinates are not converted again, nor its hues
// fixed by a method the mix does not keep.
bool mtwnValidColorMix(const mtwnColorMix* mix);

// A rotation mix's ends of unit length to within the rounding of their
// normalization (s_unitTolerance in rotation_check.c), its rotations their ends'
// floats, and its angle from 0 to pi. The angle is not taken again from
// the ends, a root and an arctangent a sample does not pay for.
bool mtwnValidRotationMix(const mtwnRotationMix* mix);

// A scale mix's ends finite and above 0, and its logarithms finite; not
// taken again, two logarithms that would cost as much as the sample.
bool mtwnValidScaleMix(const mtwnScaleMix* mix);

// A zoom mix's views as mtwnMakeZoomMix takes them and their pan finite,
// rho finite and above 0, its length finite and not below 0, r0 within
// the exponential's range, cosh r0 finite and at least 1, its pan's scale
// finite, and without a pan its widths a valid scale mix from one view's
// width to the other's. The path's parameters are not derived again.
bool mtwnValidZoomMix(const mtwnZoomMix* mix);

// A punch's fields, its maker's arguments, as mtwnMakePunch takes them.
bool mtwnValidPunch(const mtwnPunch* punch);

// A shake's fields, its maker's arguments, as mtwnMakeShake takes them:
// fewer than 2^53 lattice points over its duration.
bool mtwnValidShake(const mtwnShake* shake);

#endif
