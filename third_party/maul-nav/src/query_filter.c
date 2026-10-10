// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Query filters (mnav-0005).

#include "query_filter.h"

#include "maul-nav/bake.h"
#include "maul-nav/base.h"
#include "maul-nav/query.h"

#include <math.h>
#include <stdint.h>

// Every walkable area type: all but area 0.
#define WALKABLE_AREAS (~(uint64_t)1)

#define ONES4  1.0f, 1.0f, 1.0f, 1.0f
#define ONES16 ONES4, ONES4, ONES4, ONES4

// The filter a NULL one stands for, mnavDefaultQueryFilter's: every
// walkable area at a cost of 1.
static const mnavQueryFilter s_default = {MNAV_QUERY_FILTER_COOKIE,
                                          MNAV_ABI_VERSION,
                                          {ONES16, ONES16, ONES16, ONES16},
                                          WALKABLE_AREAS,
                                          ~(uint64_t)0};

static_assert(sizeof(s_default.costs) == 64 * sizeof(float), "the default lists 64 costs");

mnavResult mnavCheckFilter(const mnavQueryFilter* filter, const mnavQueryFilter** usable)
{
    if (filter == nullptr)
    {
        *usable = &s_default;
        return mnav_success;
    }
    if (filter->cookie != MNAV_QUERY_FILTER_COOKIE)
    {
        return mnav_errorInvalid;
    }
    if (filter->version != MNAV_ABI_VERSION)
    {
        return mnav_errorVersion;
    }
    for (int32_t a = 0; a < MNAV_AREA_TYPES; ++a)
    {
        float cost = filter->costs[a];
        if (!(cost >= MNAV_MIN_AREA_COST && cost <= MNAV_MAX_AREA_COST))
        {
            return mnav_errorRange;
        }
    }
    *usable = filter;
    return mnav_success;
}

double mnavCheapest(const mnavQueryFilter* filter)
{
    double cheapest = (double)INFINITY;
    for (int32_t a = 1; a < MNAV_AREA_TYPES; ++a)
    {
        double cost = (double)filter->costs[a];
        cheapest = mnavIncludes(filter, (mnavAreaType)a) && cost < cheapest ? cost : cheapest;
    }
    // With nothing included only the start polygon is usable and the
    // heuristic never matters; 1 keeps it finite.
    return cheapest < (double)INFINITY ? cheapest : 1.0;
}

bool mnavCrosses(const mnavQueryFilter* filter, mnavLinkKind kind)
{
    return kind < MNAV_LINK_KINDS && ((filter->kinds >> kind) & 1u) != 0;
}
