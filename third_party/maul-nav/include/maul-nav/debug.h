// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Debug geometry of navmeshes, links, paths and corridors, as
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

    /// Appends the committed tiles on a box of places: each polygon's
    /// detail triangles (mnav_debugPolygon, its area), its edges as lines
    /// (inner edges once, walls, tile sides) and each tile's bounds on the
    /// ground at its lowest detail height.
    ///
    /// @param navmesh The navmesh.
    /// @param tiles   The tile places drawn, or NULL for every tile.
    /// @param buffer  The buffer appended to. Left as it was on any failure but
    ///                the buffer filling (mnav_errorCapacity), which writes what
    ///                fits and counts what the whole needs.
    /// @return `mnav_success`; `mnav_errorInvalid` for a NULL argument, a
    /// range backward, or a buffer with a count out of range, an array
    /// missing or an origin not finite; `mnav_errorCapacity` when the
    /// buffer filled, its counts saying what the whole needs.
    /// @par Thread safety
    /// Safe from any thread. Any number of threads may read the navmesh
    /// at once while no commit runs on it; the buffer is used by one
    /// thread at a time.
    MNAV_NODISCARD MNAV_API mnavResult mnavNavmesh_Debug(const mnavNavmesh* navmesh,
                                                         const mnavTileRange* tiles,
                                                         mnavDebugBuffer* buffer);

    /// Appends every committed off-mesh link as an arc of eight lines from
    /// its start to its end, rising a quarter of its length: at its
    /// snapped points when attached, its defined ones otherwise.
    ///
    /// @param navmesh The navmesh.
    /// @param buffer  The buffer appended to. Left as it was on any failure but
    ///                the buffer filling (mnav_errorCapacity), which writes what
    ///                fits and counts what the whole needs.
    /// @return As mnavNavmesh_Debug.
    /// @par Thread safety
    /// Safe from any thread. Any number of threads may read the navmesh
    /// at once while no commit runs on it; the buffer is used by one
    /// thread at a time.
    MNAV_NODISCARD MNAV_API mnavResult mnavNavmesh_DebugLinks(const mnavNavmesh* navmesh,
                                                              mnavDebugBuffer* buffer);

    /// Appends a path's straight line, point to point. The path may be the
    /// caller's own: its points are the caller's memory, read only from
    /// `points` for `pointCount` points, each checked, and nothing else of
    /// it is read.
    ///
    /// @param path   The path.
    /// @param buffer The buffer appended to. Left as it was on any failure but
    ///               the buffer filling (mnav_errorCapacity), which writes what
    ///               fits and counts what the whole needs.
    /// @return `mnav_success`; `mnav_errorInvalid` for a NULL argument, a
    /// path with a negative count, no points or a point that is not finite,
    /// or a buffer as mnavNavmesh_Debug refuses it; `mnav_errorCapacity`
    /// when the buffer filled.
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
    /// @param buffer   The buffer appended to. Left as it was on any failure but
    ///                 the buffer filling (mnav_errorCapacity), which writes what
    ///                 fits and counts what the whole needs.
    /// @return As mnavNavmesh_Debug, and `mnav_errorInvalid` for a polygon id
    /// never handed out.
    /// @par Thread safety
    /// Safe from any thread. Any number of threads may read the navmesh
    /// at once while no commit runs on it; the buffer is used by one
    /// thread at a time.
    MNAV_NODISCARD MNAV_API mnavResult mnavNavmesh_DebugCorridor(const mnavNavmesh* navmesh,
                                                                 const mnavPolygonId* polygons,
                                                                 int32_t count,
                                                                 mnavDebugBuffer* buffer);

#ifdef __cplusplus
}
#endif

#endif // MAUL_NAV_DEBUG_H
