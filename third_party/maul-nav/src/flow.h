// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Flow fields (mnav-0007): the field, its search and its repairs, shared
// by flow.c and flow_repair.c.

#ifndef MAUL_NAV_SRC_FLOW_H
#define MAUL_NAV_SRC_FLOW_H

#include "allocator.h"

#include "maul-nav/flow.h"
#include "maul-nav/query.h"

#include <stdbool.h>
#include <stdint.h>

// A cell's place in the heap when it is not in it: never reached, or done.
#define MNAV_FLOW_NOT_OPEN (-1)
#define MNAV_FLOW_DONE     (-2)

// The work on a field: none begun, under way, or ended.
enum
{
    mnav_flowNone = 0,
    mnav_flowWorking = 1,
    mnav_flowEnded = 2
};

// A cell's marks during a repair: listed as touched, raised, a new goal.
enum
{
    mnav_flowTouched = 1,
    mnav_flowRaised = 2,
    mnav_flowNewGoal = 4
};

struct mnavFlowField
{
    mnavMemory memory;
    mnavFlowFieldDef def;
    // Per cell of the region, row by row: the cost to the goals, the next
    // cell's index, the place in the heap and the repair's marks.
    double* costs;
    int32_t* next;
    int32_t* places;
    uint8_t* marks;
    int32_t* heap;
    int32_t heapCount;
    // The goals, as cells of the region, each once.
    int32_t* goals;
    int32_t goalCount;
    // A repair's touched cells, in the order touched, the raised ones
    // first, and how many have had their next cells chosen.
    int32_t* touched;
    int32_t touchedCount;
    int32_t fixed;
    bool repairing;
    // The work's region, its grid's size and cell size, and the filter.
    mnavFlowRegion region;
    int32_t gridWidth;
    int32_t gridHeight;
    float cellSize;
    mnavQueryFilter filter;
    int32_t state;
};

// A step of work: the field and the grid's areas.
typedef struct mnavFlowSearch
{
    mnavFlowField* field;
    const mnavAreaType* areas;
} mnavFlowSearch;

// Whether grid cell (x, y) lies in the region and may be walked.
bool mnavFlowOpen(const mnavFlowSearch* s, int32_t x, int32_t y);

// The area cost of a cell of the region.
double mnavFlowAreaCost(const mnavFlowSearch* s, int32_t cell);

// Whether a cell may step in direction d, 0 to 7, to the 8 neighbours;
// the neighbour and the step's length.
bool mnavFlowNeighbor(const mnavFlowSearch* s, int32_t cell, int32_t d, int32_t* other,
                      double* length);

// A step's cost from one cell to a neighbour, as the search adds it.
double mnavFlowStepCost(const mnavFlowSearch* s, int32_t from, int32_t to, double length);

// Gives a cell a lower cost by way of the next cell given, opening it
// again when it was done; a repair lists it as touched.
void mnavFlowLower(mnavFlowField* f, int32_t cell, double cost, int32_t next);

// Whether the grid is the one the work began on.
bool mnavFlowSameGrid(const mnavFlowField* f, const mnavGrid* grid);

// Chooses the next cell of up to budget touched cells and their
// neighbours; returns how many it did, and ends the repair after the
// last.
int32_t mnavFlowFix(const mnavFlowSearch* s, int32_t budget);

#endif // MAUL_NAV_SRC_FLOW_H
