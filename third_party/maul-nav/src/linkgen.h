// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Link generation with its limits given, for tests of where it stops.

#ifndef MAUL_NAV_SRC_LINKGEN_H
#define MAUL_NAV_SRC_LINKGEN_H

#include "maul-nav/base.h"
#include "maul-nav/linkgen.h"
#include "maul-nav/navmesh.h"
#include "maul-nav/query.h"

#include <stdint.h>

// A range of tile places, both ends included.
typedef struct mnavTileRange
{
    int32_t x0;
    int32_t z0;
    int32_t x1;
    int32_t z1;
} mnavTileRange;

// Where generation stops with mnav_errorLimit: past links found, or at a
// landing box meeting more than landings polygons, at most
// MNAV_MAX_LINK_LANDINGS. mnavGenerateLinks gives MNAV_MAX_LINKS and
// MNAV_MAX_LINK_LANDINGS.
typedef struct mnavLinkGenLimits
{
    int32_t links;
    int32_t landings;
} mnavLinkGenLimits;

// mnavGenerateLinks over a range, within the limits given.
mnavResult mnavGenerateLinksWithin(mnavQuery* query, const mnavNavmesh* navmesh,
                                   const mnavQueryFilter* filter, const mnavLinkGenDef* def,
                                   mnavTileRange range, mnavLinkDef* linksOut, int32_t capacity,
                                   int32_t* countOut, mnavLinkGenLimits limits);

#endif // MAUL_NAV_SRC_LINKGEN_H
