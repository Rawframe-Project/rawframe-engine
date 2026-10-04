// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// From a search's last node to its corridor and straight path.

#ifndef MAUL_NAV_SRC_FUNNEL_H
#define MAUL_NAV_SRC_FUNNEL_H

#include "query.h"

#include "maul-nav/navmesh.h"

#include <stdint.h>

// Writes the polygons from the start to node last into the corridor and
// returns how many.
int32_t mnavPathPolygons(mnavQuery* query, const mnavNavmesh* navmesh, int32_t last);

// Writes the straight path from the start to node last into the points,
// and the off-mesh links it crosses into the links; returns the points.
int32_t mnavStraighten(mnavQuery* query, const mnavNavmesh* navmesh, int32_t last,
                       int32_t* linkCount);

// Pulls the first portals of the context tight into the straight path,
// stretch by stretch between off-mesh links: the first portal is the start
// point, the last the end. Returns the points; linkCount receives the
// links.
int32_t mnavPullPortals(mnavQuery* query, const mnavNavmesh* navmesh, int32_t portals,
                        int32_t* linkCount);

#endif // MAUL_NAV_SRC_FUNNEL_H
