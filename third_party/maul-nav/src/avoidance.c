// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Avoidance sets (mnav-0006): each agent's ORCA lines against its nearest
// neighbours (RVO2 src/Agent.cc, computeNewVelocity, in binary64), its
// share of each avoidance set by priority, and neighbours found through a
// grid sorted by cell and id, so that the agents' order never matters.

#include "maul-nav/avoidance.h"

#include "allocator.h"
#include "draw.h"
#include "obstacle.h"
#include "orca.h"

#include "maul-nav/base.h"

#include <math.h>
#include <stdalign.h>
#include <stdbool.h>
#include <stdint.h>

// Marks a def built by mnavDefaultAvoidanceDef.
#define AVOIDANCE_DEF_COOKIE 0x4E415641u

// The largest coordinate an agent may have, in meters: grid cells stay
// well inside 64-bit integers.
#define MAX_COORDINATE 1.0e12

// How far right of its preferred velocity a held-back agent aims, as a
// fraction of its speed.
#define KEEP_RIGHT 0.01

// Obstacle grid entries per obstacle vertex.
#define ENTRIES_PER_VERTEX 8

// An agent's grid cell, id and index, sorted to find neighbours.
typedef struct Key
{
    int64_t x;
    int64_t y;
    uint64_t id;
    int32_t index;
} Key;

// A neighbour: its squared distance, id and index.
typedef struct Neighbor
{
    double distance;
    uint64_t id;
    int32_t index;
} Neighbor;

struct mnavAvoidance
{
    mnavMemory memory;
    mnavAvoidanceDef def;
    Key* keys;
    Key* scratch;
    Neighbor* neighbors;
    mnavObstacleVertex* vertices;
    mnavObstacleNear* near;
    mnavObstacleGrid grid;
    // Room for the obstacle lines, then the agents'.
    mnavLine* lines;
    mnavLine* projected;
};

mnavAvoidanceDef mnavDefaultAvoidanceDef(void)
{
    return (mnavAvoidanceDef){AVOIDANCE_DEF_COOKIE, {0}, {4096, 10, 4096, 16}, 10.0, 2.0, 2.0};
}

static bool Positive(double v)
{
    return isfinite(v) && v > 0.0;
}

// Whether a def's limits, distance and horizons lie in their ranges.
static bool GoodLimits(const mnavAvoidanceDef* def)
{
    const mnavAvoidanceLimits* limits = &def->limits;
    return limits->agents >= 1 && limits->agents <= MNAV_MAX_AVOIDANCE_AGENTS &&
           limits->neighbors >= 1 && limits->neighbors <= MNAV_MAX_AVOIDANCE_NEIGHBORS &&
           limits->obstacleVertices >= 0 &&
           limits->obstacleVertices <= MNAV_MAX_AVOIDANCE_VERTICES &&
           limits->obstacleNeighbors >= 1 &&
           limits->obstacleNeighbors <= MNAV_MAX_AVOIDANCE_NEIGHBORS &&
           Positive(def->neighborDistance) && Positive(def->timeHorizon) &&
           Positive(def->obstacleTimeHorizon);
}

// Allocates a set's memory for its limits.
static mnavResult Allocate(mnavAvoidance* a)
{
    const mnavAvoidanceLimits* limits = &a->def.limits;
    size_t agents = (size_t)limits->agents;
    size_t neighbors = (size_t)limits->neighbors;
    size_t vertices = (size_t)limits->obstacleVertices;
    size_t near = (size_t)limits->obstacleNeighbors;
    mnavResult result =
        mnavAllocate(&a->memory, agents, sizeof(Key), alignof(Key), (void**)&a->keys);
    if (result == mnav_success)
    {
        result = mnavAllocate(&a->memory, agents, sizeof(Key), alignof(Key), (void**)&a->scratch);
    }
    if (result == mnav_success)
    {
        result = mnavAllocate(&a->memory, neighbors, sizeof(Neighbor), alignof(Neighbor),
                              (void**)&a->neighbors);
    }
    size_t entries = vertices * ENTRIES_PER_VERTEX;
    if (result == mnav_success && vertices > 0)
    {
        result = mnavAllocate(&a->memory, vertices, sizeof(mnavObstacleVertex),
                              alignof(mnavObstacleVertex), (void**)&a->vertices);
    }
    if (result == mnav_success && vertices > 0)
    {
        result = mnavAllocate(&a->memory, vertices, sizeof(int32_t), alignof(int32_t),
                              (void**)&a->grid.stamps);
    }
    if (result == mnav_success && vertices > 0)
    {
        result = mnavAllocate(&a->memory, entries, sizeof(mnavObstacleCell),
                              alignof(mnavObstacleCell), (void**)&a->grid.cells);
    }
    if (result == mnav_success && vertices > 0)
    {
        result = mnavAllocate(&a->memory, entries, sizeof(mnavObstacleCell),
                              alignof(mnavObstacleCell), (void**)&a->grid.scratch);
    }
    a->grid.capacity = (int32_t)entries;
    if (result == mnav_success)
    {
        result = mnavAllocate(&a->memory, near, sizeof(mnavObstacleNear), alignof(mnavObstacleNear),
                              (void**)&a->near);
    }
    if (result == mnav_success)
    {
        result = mnavAllocate(&a->memory, neighbors + near, sizeof(mnavLine), alignof(mnavLine),
                              (void**)&a->lines);
    }
    if (result == mnav_success)
    {
        result = mnavAllocate(&a->memory, neighbors + near, sizeof(mnavLine), alignof(mnavLine),
                              (void**)&a->projected);
    }
    return result;
}

mnavResult mnavCreateAvoidance(const mnavAvoidanceDef* def, mnavAvoidance** avoidanceOut)
{
    if (avoidanceOut == nullptr)
    {
        return mnav_errorInvalid;
    }
    *avoidanceOut = nullptr;
    if (def == nullptr || def->cookie != AVOIDANCE_DEF_COOKIE ||
        (def->allocator.alloc == nullptr) != (def->allocator.free == nullptr))
    {
        return mnav_errorInvalid;
    }
    if (!GoodLimits(def))
    {
        return mnav_errorRange;
    }
    mnavMemory memory = mnavMakeMemory(def->allocator, UINT64_MAX);
    mnavAvoidance* a = nullptr;
    mnavResult result =
        mnavAllocate(&memory, 1, sizeof(mnavAvoidance), alignof(mnavAvoidance), (void**)&a);
    if (result != mnav_success)
    {
        return result;
    }
    *a = (mnavAvoidance){0};
    a->memory = memory;
    a->def = *def;
    result = Allocate(a);
    if (result != mnav_success)
    {
        mnavDestroyAvoidance(a);
        return result;
    }
    *avoidanceOut = a;
    return mnav_success;
}

void mnavDestroyAvoidance(mnavAvoidance* avoidance)
{
    if (avoidance == nullptr)
    {
        return;
    }
    mnavMemory memory = avoidance->memory;
    const mnavAvoidanceLimits* limits = &avoidance->def.limits;
    size_t agents = (size_t)limits->agents;
    size_t neighbors = (size_t)limits->neighbors;
    size_t near = (size_t)limits->obstacleNeighbors;
    size_t lines = neighbors + near;
    mnavRelease(&memory, avoidance->keys, agents, sizeof(Key), alignof(Key));
    mnavRelease(&memory, avoidance->scratch, agents, sizeof(Key), alignof(Key));
    mnavRelease(&memory, avoidance->neighbors, neighbors, sizeof(Neighbor), alignof(Neighbor));
    size_t vertices = (size_t)limits->obstacleVertices;
    size_t entries = vertices * ENTRIES_PER_VERTEX;
    mnavRelease(&memory, avoidance->vertices, vertices, sizeof(mnavObstacleVertex),
                alignof(mnavObstacleVertex));
    mnavRelease(&memory, avoidance->grid.stamps, vertices, sizeof(int32_t), alignof(int32_t));
    mnavRelease(&memory, avoidance->grid.cells, entries, sizeof(mnavObstacleCell),
                alignof(mnavObstacleCell));
    mnavRelease(&memory, avoidance->grid.scratch, entries, sizeof(mnavObstacleCell),
                alignof(mnavObstacleCell));
    mnavRelease(&memory, avoidance->near, near, sizeof(mnavObstacleNear),
                alignof(mnavObstacleNear));
    mnavRelease(&memory, avoidance->lines, lines, sizeof(mnavLine), alignof(mnavLine));
    mnavRelease(&memory, avoidance->projected, lines, sizeof(mnavLine), alignof(mnavLine));
    mnavRelease(&memory, avoidance, 1, sizeof(mnavAvoidance), alignof(mnavAvoidance));
}

static bool FinitePos(mnavPos2 p)
{
    return isfinite(p.x) && isfinite(p.y);
}

static bool GoodAgent(const mnavAgent* agent)
{
    return FinitePos(agent->position) && FinitePos(agent->velocity) &&
           FinitePos(agent->preferred) && fabs(agent->position.x) <= MAX_COORDINATE &&
           fabs(agent->position.y) <= MAX_COORDINATE && Positive(agent->radius) &&
           isfinite(agent->maxSpeed) && agent->maxSpeed >= 0.0 && Positive(agent->priority);
}

static bool KeyBefore(const Key* a, const Key* b)
{
    if (a->x != b->x)
    {
        return a->x < b->x;
    }
    if (a->y != b->y)
    {
        return a->y < b->y;
    }
    return a->id != b->id ? a->id < b->id : a->index < b->index;
}

// Sorts the keys, a stable bottom-up merge through the scratch.
static void SortKeys(Key* keys, Key* scratch, int32_t count)
{
    Key* from = keys;
    Key* to = scratch;
    for (int32_t width = 1; width < count; width *= 2)
    {
        for (int32_t start = 0; start < count; start += 2 * width)
        {
            int32_t middle = start + width < count ? start + width : count;
            int32_t end = start + 2 * width < count ? start + 2 * width : count;
            int32_t i = start;
            int32_t j = middle;
            for (int32_t k = start; k < end; ++k)
            {
                bool left = i < middle && (j >= end || !KeyBefore(&from[j], &from[i]));
                to[k] = left ? from[i++] : from[j++];
            }
        }
        Key* swap = from;
        from = to;
        to = swap;
    }
    for (int32_t k = 0; from != keys && k < count; ++k)
    {
        keys[k] = from[k];
    }
}

// The first key at or after cell (x, y).
static int32_t FirstAt(const Key* keys, int32_t count, int64_t x, int64_t y)
{
    int32_t low = 0;
    int32_t high = count;
    while (low < high)
    {
        int32_t middle = low + (high - low) / 2;
        bool before = keys[middle].x < x || (keys[middle].x == x && keys[middle].y < y);
        low = before ? middle + 1 : low;
        high = before ? high : middle;
    }
    return low;
}

static bool NeighborBefore(const Neighbor* a, const Neighbor* b)
{
    if (a->distance != b->distance)
    {
        return a->distance < b->distance;
    }
    return a->id != b->id ? a->id < b->id : a->index < b->index;
}

// Keeps a candidate among the nearest, up to the limit; returns the count.
static int32_t Insert(Neighbor* list, int32_t count, int32_t limit, Neighbor candidate)
{
    if (count == limit && !NeighborBefore(&candidate, &list[count - 1]))
    {
        return count;
    }
    int32_t i = count < limit ? count++ : count - 1;
    while (i > 0 && NeighborBefore(&candidate, &list[i - 1]))
    {
        list[i] = list[i - 1];
        i -= 1;
    }
    list[i] = candidate;
    return count;
}

static int64_t CellOf(double v, double size)
{
    return (int64_t)floor(v / size);
}

// The neighbours of agent i, nearest first.
static int32_t Neighbors(mnavAvoidance* a, const mnavAgent* agents, int32_t count, int32_t i)
{
    double range = a->def.neighborDistance;
    const mnavAgent* self = &agents[i];
    int64_t cx = CellOf(self->position.x, range);
    int64_t cy = CellOf(self->position.y, range);
    int32_t found = 0;
    for (int64_t x = cx - 1; x <= cx + 1; ++x)
    {
        for (int32_t k = FirstAt(a->keys, count, x, cy - 1);
             k < count && a->keys[k].x == x && a->keys[k].y <= cy + 1; ++k)
        {
            int32_t j = a->keys[k].index;
            double dx = agents[j].position.x - self->position.x;
            double dy = agents[j].position.y - self->position.y;
            double distance = dx * dx + dy * dy;
            if (j != i && distance < range * range)
            {
                found = Insert(a->neighbors, found, a->def.limits.neighbors,
                               (Neighbor){distance, agents[j].id, j});
            }
        }
    }
    return found;
}

// The new velocity of agent i: its obstacle lines, then its neighbours',
// the 2D program over them all and the 3D one when they leave nothing.
static mnavPos2 Solve(mnavAvoidance* a, const mnavAgent* agents, int32_t agentCount, double step,
                      int32_t i)
{
    const mnavAgent* self = &agents[i];
    int32_t near = mnavNearObstacles(self, a->vertices, &a->grid, a->def.obstacleTimeHorizon,
                                     a->near, a->def.limits.obstacleNeighbors);
    int32_t fixed = mnavObstacleLines(self, a->vertices, a->near, near, a->def.obstacleTimeHorizon,
                                      step, a->lines);
    int32_t count = fixed;
    int32_t neighbors = Neighbors(a, agents, agentCount, i);
    for (int32_t n = 0; n < neighbors; ++n)
    {
        const mnavAgent* other = &agents[a->neighbors[n].index];
        a->lines[count++] = mnavPairLine(self->position, self->velocity, other->position,
                                         other->velocity, self->radius + other->radius,
                                         other->priority / (self->priority + other->priority),
                                         a->def.timeHorizon, step, self->id < other->id);
    }
    mnavPos2 velocity = {0.0, 0.0};
    int32_t failed =
        mnavLinearProgram2(a->lines, count, self->maxSpeed, self->preferred, false, &velocity);
    if (failed == count && (velocity.x != self->preferred.x || velocity.y != self->preferred.y))
    {
        // Held back: aim a little right of the preferred velocity, so that
        // agents meeting in perfect symmetry pass on their right rather
        // than stop face to face.
        mnavPos2 biased = {self->preferred.x + KEEP_RIGHT * self->preferred.y,
                           self->preferred.y - KEEP_RIGHT * self->preferred.x};
        failed = mnavLinearProgram2(a->lines, count, self->maxSpeed, biased, false, &velocity);
    }
    if (failed < count)
    {
        mnavLinearProgram3(a->lines, count, fixed, failed, self->maxSpeed, a->projected, &velocity);
    }
    return velocity;
}

mnavResult mnavAvoid(mnavAvoidance* avoidance, const mnavAgent* agents, int32_t agentCount,
                     const mnavObstacle* obstacles, int32_t obstacleCount, double step,
                     mnavPos2* velocitiesOut)
{
    if (avoidance == nullptr || agentCount < 0 || obstacleCount < 0 ||
        (agentCount > 0 && (agents == nullptr || velocitiesOut == nullptr)) ||
        (obstacleCount > 0 && obstacles == nullptr) || !Positive(step))
    {
        return mnav_errorInvalid;
    }
    if (agentCount > avoidance->def.limits.agents)
    {
        return mnav_errorLimit;
    }
    double range = avoidance->def.neighborDistance;
    for (int32_t i = 0; i < agentCount; ++i)
    {
        if (!GoodAgent(&agents[i]))
        {
            return mnav_errorInvalid;
        }
        avoidance->keys[i] = (Key){CellOf(agents[i].position.x, range),
                                   CellOf(agents[i].position.y, range), agents[i].id, i};
    }
    int32_t vertexCount = 0;
    mnavResult result = mnavBuildObstacles(obstacles, obstacleCount, avoidance->vertices,
                                           avoidance->def.limits.obstacleVertices, &vertexCount);
    if (result != mnav_success)
    {
        return result;
    }
    mnavBuildObstacleGrid(&avoidance->grid, avoidance->vertices, vertexCount,
                          avoidance->def.neighborDistance);
    SortKeys(avoidance->keys, avoidance->scratch, agentCount);
    for (int32_t i = 0; i < agentCount; ++i)
    {
        velocitiesOut[i] = Solve(avoidance, agents, agentCount, step, i);
    }
    return mnav_success;
}

// The corners of a 16-gon on the unit circle, written out so that every
// platform has the same.
static const double s_outline[16][2] = {
    {1.0, 0.0},
    {0.9238795325112867, 0.3826834323650898},
    {0.7071067811865476, 0.7071067811865475},
    {0.38268343236508984, 0.9238795325112867},
    {0.0, 1.0},
    {-0.3826834323650897, 0.9238795325112867},
    {-0.7071067811865475, 0.7071067811865476},
    {-0.9238795325112867, 0.3826834323650899},
    {-1.0, 0.0},
    {-0.9238795325112868, -0.38268343236508967},
    {-0.7071067811865477, -0.7071067811865475},
    {-0.38268343236509034, -0.9238795325112865},
    {0.0, -1.0},
    {0.38268343236509, -0.9238795325112866},
    {0.7071067811865474, -0.7071067811865477},
    {0.9238795325112865, -0.3826834323650904},
};

mnavResult mnavDebugAvoidance(mnavAvoidance* avoidance, const mnavAgent* agents, int32_t agentCount,
                              double height, mnavDebugBuffer* buffer)
{
    if (avoidance == nullptr || agentCount < 0 || (agentCount > 0 && agents == nullptr) ||
        !isfinite(height) || !mnavGoodBuffer(buffer))
    {
        return mnav_errorInvalid;
    }
    if (agentCount > avoidance->def.limits.agents)
    {
        return mnav_errorLimit;
    }
    double range = avoidance->def.neighborDistance;
    for (int32_t i = 0; i < agentCount; ++i)
    {
        if (!GoodAgent(&agents[i]))
        {
            return mnav_errorInvalid;
        }
        avoidance->keys[i] = (Key){CellOf(agents[i].position.x, range),
                                   CellOf(agents[i].position.y, range), agents[i].id, i};
    }
    SortKeys(avoidance->keys, avoidance->scratch, agentCount);
    for (int32_t i = 0; i < agentCount; ++i)
    {
        const mnavAgent* a = &agents[i];
        for (int32_t k = 0; k < 16; ++k)
        {
            mnavPos3 p = {a->position.x + a->radius * s_outline[k][0], height,
                          a->position.y + a->radius * s_outline[k][1]};
            mnavPos3 q = {a->position.x + a->radius * s_outline[(k + 1) % 16][0], height,
                          a->position.y + a->radius * s_outline[(k + 1) % 16][1]};
            mnavDrawLine(buffer, p, q, mnav_debugAgent, 0);
        }
        int32_t count = Neighbors(avoidance, agents, agentCount, i);
        for (int32_t n = 0; n < count; ++n)
        {
            const mnavAgent* b = &agents[avoidance->neighbors[n].index];
            mnavDrawLine(buffer, (mnavPos3){a->position.x, height, a->position.y},
                         (mnavPos3){b->position.x, height, b->position.y}, mnav_debugNeighbor, 0);
        }
    }
    return mnavDrawResult(buffer);
}
