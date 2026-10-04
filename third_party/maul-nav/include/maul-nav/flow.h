// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Flow fields (mnav-0007): for every cell of a grid, the cost of its
// cheapest way to the nearest of a set of goals and the next cell on it,
// so that any number of agents sharing the goals find their way at the
// cost of one search.

#ifndef MAUL_NAV_FLOW_H
#define MAUL_NAV_FLOW_H

#include "maul-nav/base.h"
#include "maul-nav/draw.h"
#include "maul-nav/query.h"

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

// The most cells a flow field may hold.
#define MNAV_MAX_FLOW_CELLS 16777216

    // How a flow field is made. Build it with mnavDefaultFlowFieldDef.
    typedef struct mnavFlowFieldDef
    {
        uint32_t cookie;
        // The allocator the field uses; zeroed for the C library's.
        mnavAllocator allocator;
        // The most cells a grid may have, 1 to MNAV_MAX_FLOW_CELLS.
        int32_t cells;
    } mnavFlowFieldDef;

    // A flow field: the memory for its cells, and the field last built.
    typedef struct mnavFlowField mnavFlowField;

    // A cell's way to the goals: its cost, infinite where no goal is
    // reached, and the next cell, the cell itself at a goal and where no
    // goal is reached.
    typedef struct mnavFlow
    {
        double cost;
        mnavCell next;
    } mnavFlow;

    /// Returns the default flow field def: up to 65536 cells.
    ///
    /// @return The def.
    /// @par Thread safety
    /// Safe from any thread.
    MNAV_API mnavFlowFieldDef mnavDefaultFlowFieldDef(void);

    /// Makes a flow field with the memory its cell limit needs.
    ///
    /// @param def      The def, from mnavDefaultFlowFieldDef.
    /// @param fieldOut Receives the field, or NULL on failure.
    /// @return `mnav_success`; `mnav_errorInvalid` for a NULL argument or a
    /// def not from mnavDefaultFlowFieldDef; `mnav_errorRange` for a cell
    /// limit out of its range; `mnav_errorCapacity` when the allocator
    /// fails.
    /// @par Thread safety
    /// Safe from any thread.
    MNAV_NODISCARD MNAV_API mnavResult mnavCreateFlowField(const mnavFlowFieldDef* def,
                                                           mnavFlowField** fieldOut);

    /// Destroys a flow field.
    ///
    /// @param field The field, or NULL.
    /// @par Thread safety
    /// Safe from any thread; the field is used by one thread at a time.
    MNAV_API void mnavDestroyFlowField(mnavFlowField* field);

    // A window of a grid's cells: columns x to x + width - 1 and rows y to
    // y + height - 1.
    typedef struct mnavFlowRegion
    {
        int32_t x;
        int32_t y;
        int32_t width;
        int32_t height;
    } mnavFlowRegion;

    /// Begins building the field for a region of a grid and a set of goal
    /// cells (mnav-0007): one search from all the goals, with the steps of
    /// mnavFindGridPath, to the 8 neighbours, never across a blocked
    /// corner, a step costing its length times the mean of its two cells'
    /// area costs. Cells outside the region are as blocked. A cell's way
    /// to the goals is a cheapest one, ties going to the neighbour found
    /// first by cost, then by index; the same grid, region and goals give
    /// the same field on every platform, however the work is divided.
    /// Blocked goals and goals outside the region are left out.
    /// mnavContinueFlowField does the work.
    ///
    /// @param field     The field; its last field is dropped.
    /// @param grid      The grid; its areas are read until the work ends.
    /// @param filter    The areas usable and their costs, or NULL; copied.
    /// @param region    The region, or NULL for the whole grid.
    /// @param goals     The goal cells, in grid places.
    /// @param goalCount How many, at least 0.
    /// @return `mnav_success`; `mnav_errorInvalid` for a NULL argument, a
    /// negative count, a grid with no areas, a side out of range or a cell
    /// size not more than 0 or not finite, a region empty or not within
    /// the grid, a goal outside the grid, or a filter not built from
    /// mnavDefaultQueryFilter; `mnav_errorRange` for a filter cost out of
    /// its range; `mnav_errorLimit` for a region of more cells than the
    /// field's limit. On an error the field holds nothing.
    /// @par Thread safety
    /// Safe from any thread; the field is used by one thread at a time.
    MNAV_NODISCARD MNAV_API mnavResult mnavBeginFlowField(mnavFlowField* field,
                                                          const mnavGrid* grid,
                                                          const mnavQueryFilter* filter,
                                                          const mnavFlowRegion* region,
                                                          const mnavCell* goals, int32_t goalCount);

    /// Continues the work begun on a field: settles up to a number of
    /// cells, fewer when the work ends first.
    ///
    /// @param field    The field.
    /// @param grid     The grid the work began on, its areas unchanged;
    ///                 it may lie elsewhere in memory.
    /// @param cells    The most cells to settle, at least 1.
    /// @param endedOut Receives whether the work has ended. May be NULL.
    /// @return `mnav_success`, also when the work had already ended;
    /// `mnav_errorInvalid` for a NULL field or grid, no work begun, fewer
    /// than 1 cell, or a grid of another size or cell size.
    /// @par Thread safety
    /// Safe from any thread; the field is used by one thread at a time.
    MNAV_NODISCARD MNAV_API mnavResult mnavContinueFlowField(mnavFlowField* field,
                                                             const mnavGrid* grid, int32_t cells,
                                                             bool* endedOut);

    /// Begins repairing the field after its goals or the areas of some of
    /// its grid's cells changed (mnav-0007): only the cells whose ways
    /// change are searched again, and when the work ends the field is the
    /// one mnavBeginFlowField would make with the new goals and areas, bit
    /// for bit. The cells whose ways ran through what changed are reset in
    /// this call; mnavContinueFlowField does the rest by its budget.
    ///
    /// @param field        The field; its work must have ended.
    /// @param grid         The grid the field was begun on, of the same
    ///                     size and cell size, its areas already changed;
    ///                     read until the work ends.
    /// @param goals        The goal cells now, in grid places.
    /// @param goalCount    How many, at least 0.
    /// @param changed      The cells whose areas changed since the field
    ///                     was last begun or repaired; each once or more,
    ///                     those outside the region ignored.
    /// @param changedCount How many, at least 0.
    /// @return `mnav_success`; `mnav_errorInvalid` for a NULL argument with
    /// a count, a negative count, nothing begun, a grid of another size or
    /// cell size or with no areas, or a goal or changed cell outside the
    /// grid; `mnav_errorStale` while work on the field has not ended.
    /// @par Thread safety
    /// Safe from any thread; the field is used by one thread at a time.
    MNAV_NODISCARD MNAV_API mnavResult mnavUpdateFlowField(mnavFlowField* field,
                                                           const mnavGrid* grid,
                                                           const mnavCell* goals, int32_t goalCount,
                                                           const mnavCell* changed,
                                                           int32_t changedCount);

    /// Builds the field for a whole grid and a set of goal cells: begins as
    /// mnavBeginFlowField and continues to the end.
    ///
    /// @param field     The field; its last field is replaced.
    /// @param grid      The grid, read only during the call.
    /// @param filter    The areas usable and their costs, or NULL.
    /// @param goals     The goal cells.
    /// @param goalCount How many, at least 0.
    /// @return As mnavBeginFlowField.
    /// @par Thread safety
    /// Safe from any thread; the field is used by one thread at a time.
    MNAV_NODISCARD MNAV_API mnavResult mnavBuildFlowField(mnavFlowField* field,
                                                          const mnavGrid* grid,
                                                          const mnavQueryFilter* filter,
                                                          const mnavCell* goals, int32_t goalCount);

    /// Reads a cell's way to the goals from the field.
    ///
    /// @param field   The field.
    /// @param cell    The cell, in grid places.
    /// @param flowOut Receives the cell's way, its next cell in grid places.
    /// @return `mnav_success`; `mnav_errorInvalid` for a NULL argument, a
    /// cell outside the field's region, or nothing begun;
    /// `mnav_errorStale` while work begun on the field has not ended.
    /// @par Thread safety
    /// Safe from any thread. Any number of threads may read a field at once
    /// while no work runs on it.
    MNAV_NODISCARD MNAV_API mnavResult mnavFlowAt(const mnavFlowField* field, mnavCell cell,
                                                  mnavFlow* flowOut);

    /// Appends an arrow for each cell of the field that has a next cell
    /// (mnav_debugFlow): from the cell's center toward the next one's, 0.8
    /// of a cell long, with two barbs, at a height, grid cell (x, y) lying
    /// at ground X and Z by the grid's cell size.
    ///
    /// @param field  The field.
    /// @param height The arrows' height.
    /// @param buffer The buffer appended to.
    /// @return `mnav_success`, appending nothing when nothing was begun;
    /// `mnav_errorInvalid` for a NULL argument, a height not finite, or a
    /// buffer with a count out of range, an array missing or an origin not
    /// finite; `mnav_errorStale` while work begun on the field has not
    /// ended; `mnav_errorCapacity` when the buffer filled, its counts
    /// saying what the whole needs.
    /// @par Thread safety
    /// Safe from any thread. Any number of threads may read a field at once
    /// while no work runs on it; the buffer is used by one thread at a
    /// time.
    MNAV_NODISCARD MNAV_API mnavResult mnavDebugFlowField(const mnavFlowField* field, double height,
                                                          mnavDebugBuffer* buffer);

#ifdef __cplusplus
}
#endif

#endif // MAUL_NAV_FLOW_H
