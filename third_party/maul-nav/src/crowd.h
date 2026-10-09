// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The neighbour grid of avoidance (mnav-0006): agents' keys sorted by
// cell and id, each occupied cell's run of them found by a hash, and the
// nearest neighbours of an agent found in rings of squares or shells of
// cubes round its own cell, so that the agents' order never matters.

#ifndef MAUL_NAV_SRC_CROWD_H
#define MAUL_NAV_SRC_CROWD_H

#include "maul-nav/base.h"

#include <stdbool.h>
#include <stdint.h>

// An agent's grid cell, id and index, sorted to find neighbours. On the
// ground plane z is 0, in both the cell and the position.
typedef struct mnavCrowdKey
{
    int64_t x;
    int64_t y;
    int64_t z;
    uint64_t id;
    int32_t index;
    // The agent's position, read in key order by the neighbour search.
    mnavPos3 position;
} mnavCrowdKey;

// An occupied cell's keys, first to end; first is -1 in a free slot.
typedef struct mnavCrowdRun
{
    int32_t first;
    int32_t end;
} mnavCrowdRun;

// A neighbour: its squared distance, id and index.
typedef struct mnavCrowdNeighbor
{
    double distance;
    uint64_t id;
    int32_t index;
} mnavCrowdNeighbor;

// The grid's arrays, which its owner allocates: keys and scratch for
// the agents, the table for mnavCrowdTableSize of them, the neighbours
// for the limit.
typedef struct mnavCrowd
{
    mnavCrowdKey* keys;
    mnavCrowdKey* scratch;
    mnavCrowdRun* table;
    uint32_t tableMask;
    mnavCrowdNeighbor* neighbors;
    // Neighbours kept, at least 1.
    int32_t limit;
    // How far away, center to center, an agent counts as a neighbour.
    double range;
    // Whether cells are cubes; squares on the ground plane.
    bool space;
} mnavCrowd;

// The cell table's size for a count of agents: a power of two at least
// twice it.
int32_t mnavCrowdTableSize(int32_t agents);

// The key of an agent at a position, its z 0 on the ground plane.
mnavCrowdKey mnavCrowdKeyOf(const mnavCrowd* crowd, mnavPos3 position, uint64_t id, int32_t index);

// Sorts the count keys filled in and builds the cell table.
void mnavSortCrowd(mnavCrowd* crowd, int32_t count);

// The neighbours of agent index at position into crowd->neighbors,
// nearest first, those at equal distances by id and index; returns how
// many.
int32_t mnavCrowdNeighbors(mnavCrowd* crowd, mnavPos3 position, int32_t index);

#endif // MAUL_NAV_SRC_CROWD_H
