// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Avoidance sets: their memory, and on the ground plane each
// agent's ORCA lines against its nearest neighbours (RVO2 src/Agent.cc,
// computeNewVelocity, in binary64), its share of each avoidance set by
// priority, and neighbours found through the sorted grid of crowd.h, so
// that the agents' order never matters.

#include "maul-nav/avoidance.h"

#include "allocator.h"
#include "avoidance.h"
#include "crowd.h"
#include "draw.h"
#include "fpenv.h"
#include "invariant.h"
#include "obstacle.h"
#include "orca.h"
#include "orca3.h"

#include "maul-nav/base.h"

#include <math.h>
#include <stdalign.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

// Obstacle grid entries per obstacle vertex.
#define ENTRIES_PER_VERTEX 8

// The def's limits, distance and horizons: mnav_errorInvalid for a count
// below 1 (0 for obstacle vertices) or a distance not positive and finite,
// mnav_errorLimit past an MNAV_MAX_ value; the horizons as mnavAvoidTime.
static mnavResult CheckLimits(const mnavAvoidanceDef* def)
{
    const mnavAvoidanceLimits* limits = &def->limits;
    if (limits->agents < 1 || limits->neighbors < 1 || limits->obstacleVertices < 0 ||
        limits->obstacleNeighbors < 1 || !mnavAvoidPositive(def->neighborDistance))
    {
        return mnav_errorInvalid;
    }
    bool within = limits->agents <= MNAV_MAX_AVOIDANCE_AGENTS &&
                  limits->neighbors <= MNAV_MAX_AVOIDANCE_NEIGHBORS &&
                  limits->obstacleVertices <= MNAV_MAX_AVOIDANCE_VERTICES &&
                  limits->obstacleNeighbors <= MNAV_MAX_AVOIDANCE_NEIGHBORS;
    mnavResult result =
        mnavAvoidJoin(mnavAvoidTime(def->timeHorizon), mnavAvoidTime(def->obstacleTimeHorizon));
    return mnavAvoidJoin(result, within ? mnav_success : mnav_errorLimit);
}

// Allocates the neighbour grid's arrays for the set's limits.
static mnavResult AllocateCrowd(mnavAvoidance* a)
{
    const mnavAvoidanceLimits* limits = &a->def.limits;
    size_t agents = (size_t)limits->agents;
    mnavCrowd* crowd = &a->crowd;
    crowd->limit = limits->neighbors;
    crowd->range = a->def.neighborDistance;
    mnavResult result = mnavAllocate(&a->memory, agents, sizeof(mnavCrowdKey),
                                     alignof(mnavCrowdKey), (void**)&crowd->keys);
    if (result == mnav_success)
    {
        result = mnavAllocate(&a->memory, agents, sizeof(mnavCrowdKey), alignof(mnavCrowdKey),
                              (void**)&crowd->scratch);
    }
    a->tableCapacity = mnavCrowdTableSize(limits->agents);
    if (result == mnav_success)
    {
        result = mnavAllocate(&a->memory, (size_t)a->tableCapacity, sizeof(mnavCrowdRun),
                              alignof(mnavCrowdRun), (void**)&crowd->table);
    }
    if (result == mnav_success)
    {
        result = mnavAllocate(&a->memory, (size_t)limits->neighbors, sizeof(mnavCrowdNeighbor),
                              alignof(mnavCrowdNeighbor), (void**)&crowd->neighbors);
    }
    return result;
}

// Allocates the obstacles' vertices and grid for the set's limits.
static mnavResult AllocateObstacles(mnavAvoidance* a)
{
    size_t vertices = (size_t)a->def.limits.obstacleVertices;
    size_t entries = vertices * ENTRIES_PER_VERTEX;
    a->grid.capacity = (int32_t)entries;
    if (vertices == 0)
    {
        return mnav_success;
    }
    mnavResult result = mnavAllocate(&a->memory, vertices, sizeof(mnavObstacleVertex),
                                     alignof(mnavObstacleVertex), (void**)&a->vertices);
    if (result == mnav_success)
    {
        result = mnavAllocate(&a->memory, vertices, sizeof(int32_t), alignof(int32_t),
                              (void**)&a->grid.stamps);
    }
    if (result == mnav_success)
    {
        result = mnavAllocate(&a->memory, entries, sizeof(mnavObstacleCell),
                              alignof(mnavObstacleCell), (void**)&a->grid.cells);
    }
    if (result == mnav_success)
    {
        result = mnavAllocate(&a->memory, entries, sizeof(mnavObstacleCell),
                              alignof(mnavObstacleCell), (void**)&a->grid.scratch);
    }
    return result;
}

// Allocates the linear programs' room for the set's limits: the near
// obstacles, and the lines and planes with their projections.
static mnavResult AllocatePrograms(mnavAvoidance* a)
{
    size_t near = (size_t)a->def.limits.obstacleNeighbors;
    size_t lines = (size_t)a->def.limits.neighbors + near;
    mnavResult result = mnavAllocate(&a->memory, near, sizeof(mnavObstacleNear),
                                     alignof(mnavObstacleNear), (void**)&a->near);
    if (result == mnav_success)
    {
        result =
            mnavAllocate(&a->memory, lines, sizeof(mnavLine), alignof(mnavLine), (void**)&a->lines);
    }
    if (result == mnav_success)
    {
        result = mnavAllocate(&a->memory, lines, sizeof(mnavLine), alignof(mnavLine),
                              (void**)&a->projected);
    }
    if (result == mnav_success)
    {
        result = mnavAllocate(&a->memory, lines, sizeof(mnavPlane), alignof(mnavPlane),
                              (void**)&a->planes);
    }
    if (result == mnav_success)
    {
        result = mnavAllocate(&a->memory, lines, sizeof(mnavPlane), alignof(mnavPlane),
                              (void**)&a->projectedPlanes);
    }
    return result;
}

static MNAV_NOINLINE mnavResult CreateAvoidanceWork(const mnavAvoidanceDef* def,
                                                    mnavAvoidance** avoidanceOut)
{
    if (avoidanceOut == nullptr)
    {
        return mnav_errorInvalid;
    }
    *avoidanceOut = nullptr;
    if (def == nullptr || def->cookie != MNAV_AVOIDANCE_DEF_COOKIE)
    {
        return mnav_errorInvalid;
    }
    if (def->version != MNAV_ABI_VERSION)
    {
        return mnav_errorVersion;
    }
    if ((def->allocator.alloc == nullptr) != (def->allocator.free == nullptr))
    {
        return mnav_errorInvalid;
    }
    mnavResult checked = CheckLimits(def);
    if (checked != mnav_success)
    {
        return checked;
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
    a->memory.host = &a->host;
    a->def = *def;
    result = AllocateCrowd(a);
    if (result == mnav_success)
    {
        result = AllocateObstacles(a);
    }
    if (result == mnav_success)
    {
        result = AllocatePrograms(a);
    }
    if (result != mnav_success)
    {
        mnavDestroyAvoidance(a);
        return result;
    }
    *avoidanceOut = a;
    return mnav_success;
}

mnavResult mnavCreateAvoidance(const mnavAvoidanceDef* def, mnavAvoidance** avoidanceOut)
{
    uint64_t env = mnavFloatEnvEnter();
    mnavResult result = CreateAvoidanceWork(def, avoidanceOut);
    mnavFloatEnvLeave(env);
    return result;
}

uint64_t mnavGetAvoidanceMisuse(const mnavAvoidance* avoidance)
{
    return avoidance != nullptr ? mnavHostMisuse(&avoidance->host) : 0;
}

void mnavDestroyAvoidance(mnavAvoidance* avoidance)
{
    if (avoidance == nullptr || mnavHostCalledBack(&avoidance->host))
    {
        return;
    }
    mnavMemory memory = avoidance->memory;
    const mnavAvoidanceLimits* limits = &avoidance->def.limits;
    size_t agents = (size_t)limits->agents;
    size_t neighbors = (size_t)limits->neighbors;
    size_t near = (size_t)limits->obstacleNeighbors;
    size_t lines = neighbors + near;
    mnavCrowd* crowd = &avoidance->crowd;
    mnavRelease(&memory, crowd->keys, agents, sizeof(mnavCrowdKey), alignof(mnavCrowdKey));
    mnavRelease(&memory, crowd->scratch, agents, sizeof(mnavCrowdKey), alignof(mnavCrowdKey));
    mnavRelease(&memory, crowd->table, (size_t)avoidance->tableCapacity, sizeof(mnavCrowdRun),
                alignof(mnavCrowdRun));
    mnavRelease(&memory, crowd->neighbors, neighbors, sizeof(mnavCrowdNeighbor),
                alignof(mnavCrowdNeighbor));
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
    mnavRelease(&memory, avoidance->planes, lines, sizeof(mnavPlane), alignof(mnavPlane));
    mnavRelease(&memory, avoidance->projectedPlanes, lines, sizeof(mnavPlane), alignof(mnavPlane));
    // The mark lives in the block being freed: it is set for good, so that
    // a call back in from this release is refused too, and the release
    // does not touch it.
    mnavHostEnter(&avoidance->host);
    memory.host = nullptr;
    mnavRelease(&memory, avoidance, 1, sizeof(mnavAvoidance), alignof(mnavAvoidance));
}

static mnavResult CheckAgent(const mnavAgent* agent)
{
    if (!(agent->maxSpeed >= 0.0) || !mnavAvoidPositive(agent->priority))
    {
        return mnav_errorInvalid;
    }
    mnavResult result = mnavAvoidJoin(mnavAvoidCoordinate(agent->position.x),
                                      mnavAvoidCoordinate(agent->position.y));
    result = mnavAvoidJoin(result, mnavAvoidJoin(mnavAvoidSpeed(agent->velocity.x),
                                                 mnavAvoidSpeed(agent->velocity.y)));
    result = mnavAvoidJoin(result, mnavAvoidJoin(mnavAvoidSpeed(agent->preferred.x),
                                                 mnavAvoidSpeed(agent->preferred.y)));
    return mnavAvoidJoin(
        result, mnavAvoidJoin(mnavAvoidRadius(agent->radius), mnavAvoidSpeed(agent->maxSpeed)));
}

// Checks the agents as hostile input and fills the ground grid's keys.
static mnavResult FillKeys(mnavAvoidance* a, const mnavAgent* agents, int32_t count)
{
    mnavBeginCrowd(&a->crowd, false);
    for (int32_t i = 0; i < count; ++i)
    {
        mnavResult result = CheckAgent(&agents[i]);
        if (result != mnav_success)
        {
            return result;
        }
        mnavPos3 position = {agents[i].position.x, agents[i].position.y, 0.0};
        a->crowd.keys[i] = mnavCrowdKeyOf(&a->crowd, position, agents[i].id, i);
        a->crowd.keys[i].layers = agents[i].layers;
    }
    return mnav_success;
}

// The new velocity of agent i: its obstacle lines, then its neighbours',
// the 2D program over them all and the 3D one when they leave nothing.
static mnavPos2 Solve(mnavAvoidance* a, const mnavAgent* agents, double step, int32_t i)
{
    const mnavAgent* self = &agents[i];
    int32_t near = mnavNearObstacles(self, a->vertices, &a->grid, a->def.obstacleTimeHorizon,
                                     a->near, a->def.limits.obstacleNeighbors);
    int32_t fixed = mnavObstacleLines(self, a->vertices, a->near, near, a->def.obstacleTimeHorizon,
                                      step, a->lines);
    int32_t count = fixed;
    int32_t neighbors = mnavCrowdNeighbors(
        &a->crowd, (mnavPos3){self->position.x, self->position.y, 0.0}, i, self->ignores);
    // A line an obstacle and an agent at most: the lines fit their buffer.
    MNAV_ASSERT(fixed <= near && near <= a->def.limits.obstacleNeighbors &&
                neighbors <= a->def.limits.neighbors);
    for (int32_t n = 0; n < neighbors; ++n)
    {
        const mnavAgent* other = &agents[a->crowd.neighbors[n].index];
        // An agent that ignores this one leaves it the whole avoidance.
        double share = (other->ignores & self->layers) != 0
                           ? 1.0
                           : other->priority / (self->priority + other->priority);
        a->lines[count++] = mnavPairLine(self->position, self->velocity, other->position,
                                         other->velocity, self->radius + other->radius, share,
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
        mnavPos2 biased = {self->preferred.x + MNAV_KEEP_RIGHT * self->preferred.y,
                           self->preferred.y - MNAV_KEEP_RIGHT * self->preferred.x};
        failed = mnavLinearProgram2(a->lines, count, self->maxSpeed, biased, false, &velocity);
    }
    if (failed < count)
    {
        mnavLinearProgram3(a->lines, count, fixed, failed, self->maxSpeed, a->projected, &velocity);
    }
    // The programs stay within the speed circle up to rounding, which
    // lines meeting at a narrow angle make larger; the maximum speed is
    // kept exactly.
    double speedSq = mnavDot2(velocity, velocity);
    if (speedSq > self->maxSpeed * self->maxSpeed)
    {
        velocity = mnavScale2(velocity, self->maxSpeed / sqrt(speedSq));
    }
    // Within the speed circle, but for the scaling's last bits: relative,
    // and a few units of the least subnormal where the squares are
    // subnormal and round absolutely.
    MNAV_ASSERT(isfinite(velocity.x) && isfinite(velocity.y) &&
                mnavDot2(velocity, velocity) <=
                    self->maxSpeed * self->maxSpeed * (1.0 + 0x1p-40) + 0x1p-1072);
    return velocity;
}

static MNAV_NOINLINE mnavResult AvoidWork(mnavAvoidance* avoidance, const mnavAgent* agents,
                                          int32_t agentCount, const mnavObstacle* obstacles,
                                          int32_t obstacleCount, double step,
                                          mnavPos2* velocitiesOut)
{
    if (avoidance != nullptr && mnavHostCalledBack(&avoidance->host))
    {
        return mnav_errorBusy;
    }
    if (avoidance == nullptr || agentCount < 0 || obstacleCount < 0 ||
        (agentCount > 0 && (agents == nullptr || velocitiesOut == nullptr)) ||
        (obstacleCount > 0 && obstacles == nullptr))
    {
        return mnav_errorInvalid;
    }
    mnavResult result = mnavAvoidTime(step);
    if (result != mnav_success)
    {
        return result;
    }
    if (agentCount > avoidance->def.limits.agents)
    {
        return mnav_errorCapacity;
    }
    result = FillKeys(avoidance, agents, agentCount);
    if (result != mnav_success)
    {
        return result;
    }
    int32_t vertexCount = 0;
    result = mnavBuildObstacles(obstacles, obstacleCount, avoidance->vertices,
                                avoidance->def.limits.obstacleVertices, &vertexCount);
    if (result != mnav_success)
    {
        return result;
    }
    mnavBuildObstacleGrid(&avoidance->grid, avoidance->vertices, vertexCount,
                          avoidance->def.neighborDistance);
    mnavSortCrowd(&avoidance->crowd, agentCount);
    for (int32_t i = 0; i < agentCount; ++i)
    {
        velocitiesOut[i] = Solve(avoidance, agents, step, i);
    }
    mnavEndCrowd(&avoidance->crowd);
    return mnav_success;
}

static MNAV_NOINLINE mnavResult AvoidCounted(mnavAvoidance* avoidance, const mnavAgent* agents,
                                             int32_t agentCount, const mnavObstacle* obstacles,
                                             int32_t obstacleCount, double step,
                                             mnavPos2* velocitiesOut)
{
    return mnavHostCountInvalid(
        MNAV_HOST(avoidance),
        AvoidWork(avoidance, agents, agentCount, obstacles, obstacleCount, step, velocitiesOut));
}

mnavResult mnavAvoid(mnavAvoidance* avoidance, const mnavAgent* agents, int32_t agentCount,
                     const mnavObstacle* obstacles, int32_t obstacleCount, double step,
                     mnavPos2* velocitiesOut)
{
    uint64_t env = mnavFloatEnvEnter();
    mnavResult result =
        AvoidCounted(avoidance, agents, agentCount, obstacles, obstacleCount, step, velocitiesOut);
    mnavFloatEnvLeave(env);
    return result;
}

const double mnavOutline[16][2] = {
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

static MNAV_NOINLINE mnavResult DebugAvoidanceWork(mnavAvoidance* avoidance,
                                                   const mnavAgent* agents, int32_t agentCount,
                                                   double height, mnavDebugBuffer* buffer)
{
    if (avoidance != nullptr && mnavHostCalledBack(&avoidance->host))
    {
        return mnav_errorBusy;
    }
    if (avoidance == nullptr || agentCount < 0 || (agentCount > 0 && agents == nullptr) ||
        !isfinite(height) || !mnavGoodBuffer(buffer))
    {
        return mnav_errorInvalid;
    }
    if (agentCount > avoidance->def.limits.agents)
    {
        return mnav_errorCapacity;
    }
    mnavResult result = FillKeys(avoidance, agents, agentCount);
    if (result != mnav_success)
    {
        return result;
    }
    mnavSortCrowd(&avoidance->crowd, agentCount);
    for (int32_t i = 0; i < agentCount; ++i)
    {
        const mnavAgent* a = &agents[i];
        for (int32_t k = 0; k < 16; ++k)
        {
            mnavPos3 p = {a->position.x + a->radius * mnavOutline[k][0], height,
                          a->position.y + a->radius * mnavOutline[k][1]};
            mnavPos3 q = {a->position.x + a->radius * mnavOutline[(k + 1) % 16][0], height,
                          a->position.y + a->radius * mnavOutline[(k + 1) % 16][1]};
            mnavDrawLine(buffer, p, q, mnav_debugAgent, 0);
        }
        int32_t count = mnavCrowdNeighbors(
            &avoidance->crowd, (mnavPos3){a->position.x, a->position.y, 0.0}, i, a->ignores);
        for (int32_t n = 0; n < count; ++n)
        {
            const mnavAgent* b = &agents[avoidance->crowd.neighbors[n].index];
            mnavDrawLine(buffer, (mnavPos3){a->position.x, height, a->position.y},
                         (mnavPos3){b->position.x, height, b->position.y}, mnav_debugNeighbor, 0);
        }
    }
    return mnavDrawResult(buffer);
}

static MNAV_NOINLINE mnavResult DebugAvoidanceCounted(mnavAvoidance* avoidance,
                                                      const mnavAgent* agents, int32_t agentCount,
                                                      double height, mnavDebugBuffer* buffer)
{
    return mnavHostCountInvalid(MNAV_HOST(avoidance),
                                DebugAvoidanceWork(avoidance, agents, agentCount, height, buffer));
}

mnavResult mnavDebugAvoidance(mnavAvoidance* avoidance, const mnavAgent* agents, int32_t agentCount,
                              double height, mnavDebugBuffer* buffer)
{
    uint64_t env = mnavFloatEnvEnter();
    mnavResult result = DebugAvoidanceCounted(avoidance, agents, agentCount, height, buffer);
    mnavFloatEnvLeave(env);
    return result;
}
