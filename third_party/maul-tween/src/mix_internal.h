// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The mix of two values.

#ifndef MAUL_TWEEN_SRC_MIX_INTERNAL_H
#define MAUL_TWEEN_SRC_MIX_INTERNAL_H

// The mix of a and b at t: a + t (b - a), and b at t = 1. Exact at t = 0
// and t = 1, a for a = b at any finite t, and monotone in t. Its error is
// at most u |r| + 2u |t (b - a)| for the result r (u = 2^-53, test_mix):
// far past near ends, where the line crosses 0, a few thousand u of r,
// where the weighted sum (1 - t) a + t b errs by about u |t a|. P0811's
// std::lerp takes this form for ends of one sign; culori's lerp is it
// without the test.
static inline double mtwnMix(double a, double b, double t)
{
    return t == 1.0 ? b : a + t * (b - a);
}

#endif
