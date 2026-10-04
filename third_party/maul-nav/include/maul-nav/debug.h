// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Debug geometry of navmeshes, links, paths and corridors (mnav-0010), as
// plain vertex and index data in a caller's buffer.

#ifndef MAUL_NAV_DEBUG_H
#define MAUL_NAV_DEBUG_H

#include "maul-nav/base.h"
#include "maul-nav/draw.h"
#include "maul-nav/navmesh.h"
#include "maul-nav/query.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    /// Appends the committed tiles on a range of places: each polygon's
    /// detail triangles (mnav_debugPolygon, its area), its edges as lines
    /// (inner edges once, walls, tile sides) and each tile's bounds on the
    /// ground at its lowest detail height.
    ///
    /// @param navmesh The navmesh.
    /// @param tileX0  The first tile column.
    /// @param tileZ0  The first tile row.
    /// @param tileX1  The last tile column, at least tileX0.
    /// @param tileZ1  The last tile row, at least tileZ0.
    /// @param buffer  The buffer appended to.
    /// @return `mnav_success`; `mnav_errorInvalid` for a NULL argument, a
    /// range backward, or a buffer with a count out of range, an array
    /// missing or an origin not finite; `mnav_errorCapacity` when the
    /// buffer filled, its counts saying what the whole needs.
    /// @par Thread safety
    /// Safe from any thread. Any number of threads may read the navmesh
    /// at once while no commit runs on it; the buffer is used by one
    /// thread at a time.
    MNAV_NODISCARD MNAV_API mnavResult mnavDebugNavmesh(const mnavNavmesh* navmesh, int32_t tileX0,
                                                        int32_t tileZ0, int32_t tileX1,
                                                        int32_t tileZ1, mnavDebugBuffer* buffer);

    /// Appends every committed off-mesh link as an arc of eight lines from
    /// its start to its end, rising a quarter of its length: at its
    /// snapped points when attached, its defined ones otherwise.
    ///
    /// @param navmesh The navmesh.
    /// @param buffer  The buffer appended to.
    /// @return As mnavDebugNavmesh.
    /// @par Thread safety
    /// Safe from any thread. Any number of threads may read the navmesh
    /// at once while no commit runs on it; the buffer is used by one
    /// thread at a time.
    MNAV_NODISCARD MNAV_API mnavResult mnavDebugLinks(const mnavNavmesh* navmesh,
                                                      mnavDebugBuffer* buffer);

    /// Appends a path's straight line, point to point.
    ///
    /// @param path   The path.
    /// @param buffer The buffer appended to.
    /// @return As mnavDebugNavmesh.
    /// @par Thread safety
    /// Safe from any thread; the buffer is used by one thread at a time.
    MNAV_NODISCARD MNAV_API mnavResult mnavDebugPath(const mnavPath* path, mnavDebugBuffer* buffer);

    /// Appends the detail triangles of a run of polygons, a path's or a
    /// corridor's, each tagged with its place in the run; polygons whose
    /// tile has gone are skipped.
    ///
    /// @param navmesh  The navmesh.
    /// @param polygons The polygons.
    /// @param count    How many, at least 0.
    /// @param buffer   The buffer appended to.
    /// @return As mnavDebugNavmesh, and `mnav_errorInvalid` for a polygon id
    /// never handed out.
    /// @par Thread safety
    /// Safe from any thread. Any number of threads may read the navmesh
    /// at once while no commit runs on it; the buffer is used by one
    /// thread at a time.
    MNAV_NODISCARD MNAV_API mnavResult mnavDebugCorridor(const mnavNavmesh* navmesh,
                                                         const mnavPolygonId* polygons,
                                                         int32_t count, mnavDebugBuffer* buffer);

#ifdef __cplusplus
}
#endif

#endif // MAUL_NAV_DEBUG_H
