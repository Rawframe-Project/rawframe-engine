// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Removing the tile-border vertices from a tile's polygons.

#ifndef MAUL_NAV_SRC_BORDER_VERTICES_H
#define MAUL_NAV_SRC_BORDER_VERTICES_H

#include "allocator.h"
#include "polymesh.h"

#include "maul-nav/bake.h"

#include <stdint.h>

// Removes each removable vertex, in index order, whose polygons share one
// area, replacing them with the merged triangulation of the hole left.
// The hole's edges must form one chain, which the polygons round a vertex
// form only when they are joined edge to edge; an open chain must close
// across the vertex's place, so the outline keeps its shape. A vertex
// whose replacement cannot be built stays, still removable.
// Polygons must not be linked yet. Returns mnav_errorLimit when the new
// polygons would pass maxPolygons.
mnavResult mnavRemoveBorderVertices(mnavMemory* memory, mnavPolyMesh* mesh, int32_t maxPolygons);

#endif // MAUL_NAV_SRC_BORDER_VERTICES_H
