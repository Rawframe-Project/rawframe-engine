// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Avoidance obstacles (mnav-0006): their vertices, the ones near an agent,
// and the ORCA lines they give, after RVO2.

#ifndef MAUL_NAV_SRC_OBSTACLE_H
#define MAUL_NAV_SRC_OBSTACLE_H

#include "orca.h"

#include "maul-nav/avoidance.h"
#include "maul-nav/base.h"

#include <stdbool.h>
#include <stdint.h>

// An obstacle vertex: a circle's center, or a segment's or polygon's
// point with the edge from it to the next.
typedef struct mnavObstacleVertex
{
    mnavPos2 point;
    // The unit direction to the next vertex.
    mnavPos2 direction;
    mnavPos2 velocity;
    // A circle's radius; 0 for an edge.
    double radius;
    uint64_t id;
    int32_t index;
    int32_t next;
    int32_t previous;
    bool convex;
} mnavObstacleVertex;

// An obstacle near an agent: its squared distance, then its vertex.
typedef struct mnavObstacleNear
{
    double distance;
    int32_t vertex;
} mnavObstacleNear;

// Checks the obstacles as hostile input and writes their vertices, at most
// capacity: mnav_errorInvalid for a bad obstacle, mnav_errorLimit for too
// many points.
mnavResult mnavBuildObstacles(const mnavObstacle* obstacles, int32_t count,
                              mnavObstacleVertex* vertices, int32_t capacity, int32_t* vertexCount);

// A circle's or an edge's place in a grid cell its bounds cover.
typedef struct mnavObstacleCell
{
    int64_t x;
    int64_t y;
    int32_t vertex;
} mnavObstacleCell;

// The obstacles' circles and edges by the grid cells their bounds cover,
// sorted by cell, then vertex, in a caller's memory: at most capacity
// entries, the cells growing until they fit; and a stamp per vertex, so
// that an agent visits each once.
typedef struct mnavObstacleGrid
{
    mnavObstacleCell* cells;
    mnavObstacleCell* scratch;
    int32_t count;
    int32_t capacity;
    int32_t* stamps;
    int32_t stamp;
    // A cell's side, and the fastest obstacle's speed.
    double size;
    double fastest;
} mnavObstacleGrid;

// Fills the grid with the vertices' circles and edges, starting from cells
// of the side given, at least 4 entries of room per vertex.
void mnavBuildObstacleGrid(mnavObstacleGrid* grid, const mnavObstacleVertex* vertices,
                           int32_t vertexCount, double size);

// The circles and edges an agent sees within reach of the horizon, the
// nearest first, at most limit; the grid narrows the candidates, the same
// for any cell size.
int32_t mnavNearObstacles(const mnavAgent* agent, const mnavObstacleVertex* vertices,
                          mnavObstacleGrid* grid, double horizon, mnavObstacleNear* list,
                          int32_t limit);

// Writes the agent's lines for the obstacles near it, skipping those the
// lines before already cover; returns how many.
int32_t mnavObstacleLines(const mnavAgent* agent, const mnavObstacleVertex* vertices,
                          const mnavObstacleNear* near, int32_t nearCount, double horizon,
                          double step, mnavLine* lines);

#endif // MAUL_NAV_SRC_OBSTACLE_H
