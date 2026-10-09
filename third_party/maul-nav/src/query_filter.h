// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Query filters (mnav-0005): checking them and reading them.

#ifndef MAUL_NAV_SRC_QUERY_FILTER_H
#define MAUL_NAV_SRC_QUERY_FILTER_H

#include "maul-nav/bake.h"
#include "maul-nav/base.h"
#include "maul-nav/query.h"

#include <stdint.h>

// Checks a caller's filter, NULL for the default, and points usable at the
// filter to use: mnav_errorInvalid for one not built from
// mnavDefaultQueryFilter, mnav_errorRange for a cost out of its range.
mnavResult mnavCheckFilter(const mnavQueryFilter* filter, const mnavQueryFilter** usable);

// Whether a filter includes an area type. No polygon has area 0. Inline:
// every search step asks it.
static inline bool mnavIncludes(const mnavQueryFilter* filter, mnavAreaType area)
{
    return area < MNAV_AREA_TYPES && ((filter->areas >> area) & 1u) != 0;
}

// Whether a filter lets the agent cross links of a kind.
bool mnavCrosses(const mnavQueryFilter* filter, mnavLinkKind kind);

// The lowest cost among the areas a filter includes, 1 when it includes
// none.
double mnavCheapest(const mnavQueryFilter* filter);

#endif // MAUL_NAV_SRC_QUERY_FILTER_H
