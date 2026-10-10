// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Avoidance in space (mnav-0006): each flier's ORCA planes against the
// spheres near it and its nearest neighbours (RVO2-3D src/Agent.cc,
// computeNewVelocity, in binary64), with the ground call's share by
// priority, sorted grid and sidestep.

#include "avoidance.h"
#include "crowd.h"
#include "draw.h"
#include "obstacle.h"
#include "orca3.h"

#include "maul-nav/avoidance.h"
#include "maul-nav/base.h"
#include "maul-nav/draw.h"

#include <math.h>
#include <stdbool.h>
#include <stdint.h>

static bool GoodVector(mnavPos3 v)
{
    return mnavAvoidSpeed(v.x) && mnavAvoidSpeed(v.y) && mnavAvoidSpeed(v.z);
}

static bool GoodPoint(mnavPos3 p)
{
    return mnavAvoidCoordinate(p.x) && mnavAvoidCoordinate(p.y) && mnavAvoidCoordinate(p.z);
}

static bool GoodAgent(const mnavAgent3D* agent)
{
    return GoodPoint(agent->position) && GoodVector(agent->velocity) &&
           GoodVector(agent->preferred) && mnavAvoidRadius(agent->radius) &&
           mnavAvoidSpeed(agent->maxSpeed) && agent->maxSpeed >= 0.0 &&
           mnavAvoidPositive(agent->priority);
}

static bool GoodSphere(const mnavSphere* sphere)
{
    return GoodPoint(sphere->center) && mnavAvoidRadius(sphere->radius) &&
           GoodVector(sphere->velocity);
}

// A sphere's reach from an agent: the squared gap between them, or -1
// when the agent cannot meet it within the horizon.
static double SphereReach(const mnavAgent3D* agent, const mnavSphere* sphere, double horizon)
{
    double speed = sqrt(mnavDot3(sphere->velocity, sphere->velocity));
    double range = horizon * (agent->maxSpeed + speed) + agent->radius;
    mnavPos3 rel = mnavSub3(sphere->center, agent->position);
    double gap = sqrt(mnavDot3(rel, rel)) - sphere->radius;
    gap = gap > 0.0 ? gap : 0.0;
    return gap < range ? gap * gap : -1.0;
}

void mnavBuildSphereGrid(mnavAvoidance* a, const mnavSphere* spheres, int32_t count)
{
    double fastest = 0.0;
    for (int32_t s = 0; s < count; ++s)
    {
        const mnavSphere* sphere = &spheres[s];
        a->vertices[s] = (mnavObstacleVertex){
            .point = {sphere->center.x, sphere->center.z},
            .velocity = {sphere->velocity.x, sphere->velocity.z},
            .radius = sphere->radius,
            .id = sphere->id,
            .index = s,
            .next = s,
            .previous = s,
        };
        double speed = sqrt(mnavDot3(sphere->velocity, sphere->velocity));
        fastest = speed > fastest ? speed : fastest;
    }
    mnavBuildObstacleGrid(&a->grid, a->vertices, count, a->def.neighborDistance);
    // A sphere's reach counts its speed in space, not on the ground.
    a->grid.fastest = fastest;
}

// An agent's view of the spheres, for measuring them.
typedef struct Seen
{
    const mnavAgent3D* agent;
    const mnavSphere* spheres;
    double horizon;
} Seen;

static double MeasureSphere(const void* context, int32_t s)
{
    const Seen* seen = context;
    if ((seen->spheres[s].layers & seen->agent->ignores) != 0)
    {
        return -1.0;
    }
    return SphereReach(seen->agent, &seen->spheres[s], seen->horizon);
}

int32_t mnavNearSpheres(mnavAvoidance* a, const mnavAgent3D* agent, const mnavSphere* spheres,
                        int32_t count, mnavObstacleNear* list)
{
    if (count == 0)
    {
        return 0;
    }
    // A sphere the agent may meet lies within its reach on the ground,
    // for the fastest sphere's speed; the reach is padded so that
    // rounding at a cell's side never drops one.
    double horizon = a->def.obstacleTimeHorizon;
    mnavPos2 p = {agent->position.x, agent->position.z};
    double reach = horizon * (agent->maxSpeed + a->grid.fastest) + agent->radius;
    reach += 0x1p-20 * (reach + fabs(p.x) + fabs(p.y));
    Seen seen = {agent, spheres, horizon};
    return mnavNearInGrid(&a->grid, a->vertices, p, reach, MeasureSphere, &seen, list,
                          a->def.limits.obstacleNeighbors);
}

// The plane of a sphere: an agent that never gives way while apart. An
// agent already overlapping it is asked to leave it within the step, but
// never faster than its maximum speed, as with a circle on the ground.
static mnavPlane SpherePlane(const mnavAgent3D* agent, const mnavSphere* sphere, double horizon,
                             double step)
{
    double combined = agent->radius + sphere->radius;
    mnavPos3 rel = mnavSub3(sphere->center, agent->position);
    double distanceSq = mnavDot3(rel, rel);
    if (distanceSq > combined * combined)
    {
        return mnavPairPlane(agent->position, agent->velocity, sphere->center, sphere->velocity,
                             combined, 1.0, horizon, step, agent->id < sphere->id);
    }
    // On the center itself no way is out; a fixed one, forward, keeps it
    // deterministic.
    mnavPos3 away = mnavNormalize3(mnavScale3(rel, -1.0));
    if (away.x == 0.0 && away.y == 0.0 && away.z == 0.0)
    {
        away = (mnavPos3){0.0, 0.0, -1.0};
    }
    double leave = mnavDot3(sphere->velocity, away) + (combined - sqrt(distanceSq)) / step;
    double offset = leave < agent->maxSpeed ? leave : agent->maxSpeed;
    return (mnavPlane){mnavScale3(away, offset), away};
}

// The preferred velocity turned 1% of its speed to its right, +Y being
// up. A vertical one has no right: rising it turns toward +X, falling
// toward -X, so that two fliers meeting head on still turn apart.
static mnavPos3 KeepRight(mnavPos3 preferred)
{
    mnavPos3 right = {-preferred.z, 0.0, preferred.x};
    if (right.x == 0.0 && right.z == 0.0)
    {
        right = (mnavPos3){preferred.y > 0.0 ? 1.0 : -1.0, 0.0, 0.0};
    }
    double speed = sqrt(mnavDot3(preferred, preferred));
    return mnavAdd3(preferred, mnavScale3(mnavNormalize3(right), MNAV_KEEP_RIGHT * speed));
}

// The new velocity of agent i: its sphere planes, then its neighbours',
// the 3D program over them all and the 4D one when they leave nothing.
static mnavPos3 Solve(mnavAvoidance* a, const mnavAgent3D* agents, const mnavSphere* spheres,
                      int32_t sphereCount, double step, int32_t i)
{
    const mnavAgent3D* self = &agents[i];
    int32_t near = mnavNearSpheres(a, self, spheres, sphereCount, a->near);
    int32_t count = 0;
    for (int32_t k = 0; k < near; ++k)
    {
        a->planes[count++] =
            SpherePlane(self, &spheres[a->near[k].vertex], a->def.obstacleTimeHorizon, step);
    }
    int32_t fixed = count;
    int32_t neighbors = mnavCrowdNeighbors(&a->crowd, self->position, i, self->ignores);
    for (int32_t n = 0; n < neighbors; ++n)
    {
        const mnavAgent3D* other = &agents[a->crowd.neighbors[n].index];
        // An agent that ignores this one leaves it the whole avoidance.
        double share = (other->ignores & self->layers) != 0
                           ? 1.0
                           : other->priority / (self->priority + other->priority);
        a->planes[count++] = mnavPairPlane(self->position, self->velocity, other->position,
                                           other->velocity, self->radius + other->radius, share,
                                           a->def.timeHorizon, step, self->id < other->id);
    }
    mnavPos3 velocity = {0.0, 0.0, 0.0};
    int32_t failed =
        mnavPlaneProgram3(a->planes, count, self->maxSpeed, self->preferred, false, &velocity);
    if (failed == count && (velocity.x != self->preferred.x || velocity.y != self->preferred.y ||
                            velocity.z != self->preferred.z))
    {
        // Held back: aim a little right of the preferred velocity, so that
        // agents meeting in perfect symmetry pass rather than stop.
        failed = mnavPlaneProgram3(a->planes, count, self->maxSpeed, KeepRight(self->preferred),
                                   false, &velocity);
    }
    if (failed < count)
    {
        mnavPlaneProgram4(a->planes, count, fixed, failed, self->maxSpeed, a->projectedPlanes,
                          &velocity);
    }
    // The maximum speed is kept exactly, as on the ground.
    double speedSq = mnavDot3(velocity, velocity);
    if (speedSq > self->maxSpeed * self->maxSpeed)
    {
        velocity = mnavScale3(velocity, self->maxSpeed / sqrt(speedSq));
    }
    return velocity;
}

// Checks a call's limits and its agents and spheres as hostile input,
// then fills and sorts the grid in cubes; the arguments' presence is the
// caller's to check.
static mnavResult Prepare(mnavAvoidance* avoidance, const mnavAgent3D* agents, int32_t agentCount,
                          const mnavSphere* spheres, int32_t sphereCount)
{
    if (agentCount > avoidance->def.limits.agents ||
        sphereCount > avoidance->def.limits.obstacleVertices)
    {
        return mnav_errorLimit;
    }
    for (int32_t s = 0; s < sphereCount; ++s)
    {
        if (!GoodSphere(&spheres[s]))
        {
            return mnav_errorInvalid;
        }
    }
    mnavCrowd* crowd = &avoidance->crowd;
    mnavBeginCrowd(crowd, true);
    for (int32_t i = 0; i < agentCount; ++i)
    {
        if (!GoodAgent(&agents[i]))
        {
            return mnav_errorInvalid;
        }
        crowd->keys[i] = mnavCrowdKeyOf(crowd, agents[i].position, agents[i].id, i);
        crowd->keys[i].layers = agents[i].layers;
    }
    mnavSortCrowd(crowd, agentCount);
    return mnav_success;
}

mnavResult mnavAvoid3D(mnavAvoidance* avoidance, const mnavAgent3D* agents, int32_t agentCount,
                       const mnavSphere* spheres, int32_t sphereCount, double step,
                       mnavPos3* velocitiesOut)
{
    if (avoidance == nullptr || agentCount < 0 || sphereCount < 0 ||
        (agentCount > 0 && (agents == nullptr || velocitiesOut == nullptr)) ||
        (sphereCount > 0 && spheres == nullptr) || !mnavAvoidTime(step))
    {
        return mnav_errorInvalid;
    }
    mnavResult result = Prepare(avoidance, agents, agentCount, spheres, sphereCount);
    if (result != mnav_success)
    {
        return result;
    }
    mnavBuildSphereGrid(avoidance, spheres, sphereCount);
    for (int32_t i = 0; i < agentCount; ++i)
    {
        velocitiesOut[i] = Solve(avoidance, agents, spheres, sphereCount, step, i);
    }
    return mnav_success;
}

// Draws three 16-gons of a radius round a center, one in each axis plane.
static void Rings(mnavDebugBuffer* buffer, mnavPos3 c, double radius, mnavDebugKind kind)
{
    for (int32_t k = 0; k < 16; ++k)
    {
        double u0 = radius * mnavOutline[k][0];
        double u1 = radius * mnavOutline[k][1];
        double w0 = radius * mnavOutline[(k + 1) % 16][0];
        double w1 = radius * mnavOutline[(k + 1) % 16][1];
        mnavDrawLine(buffer, (mnavPos3){c.x + u0, c.y + u1, c.z},
                     (mnavPos3){c.x + w0, c.y + w1, c.z}, kind, 0);
        mnavDrawLine(buffer, (mnavPos3){c.x, c.y + u0, c.z + u1},
                     (mnavPos3){c.x, c.y + w0, c.z + w1}, kind, 0);
        mnavDrawLine(buffer, (mnavPos3){c.x + u0, c.y, c.z + u1},
                     (mnavPos3){c.x + w0, c.y, c.z + w1}, kind, 0);
    }
}

mnavResult mnavDebugAvoidance3D(mnavAvoidance* avoidance, const mnavAgent3D* agents,
                                int32_t agentCount, const mnavSphere* spheres, int32_t sphereCount,
                                mnavDebugBuffer* buffer)
{
    if (avoidance == nullptr || agentCount < 0 || sphereCount < 0 ||
        (agentCount > 0 && agents == nullptr) || (sphereCount > 0 && spheres == nullptr) ||
        !mnavGoodBuffer(buffer))
    {
        return mnav_errorInvalid;
    }
    mnavResult result = Prepare(avoidance, agents, agentCount, spheres, sphereCount);
    if (result != mnav_success)
    {
        return result;
    }
    for (int32_t i = 0; i < agentCount; ++i)
    {
        const mnavAgent3D* a = &agents[i];
        Rings(buffer, a->position, a->radius, mnav_debugAgent);
        int32_t count = mnavCrowdNeighbors(&avoidance->crowd, a->position, i, a->ignores);
        for (int32_t n = 0; n < count; ++n)
        {
            const mnavAgent3D* b = &agents[avoidance->crowd.neighbors[n].index];
            mnavDrawLine(buffer, a->position, b->position, mnav_debugNeighbor, 0);
        }
    }
    for (int32_t s = 0; s < sphereCount; ++s)
    {
        Rings(buffer, spheres[s].center, spheres[s].radius, mnav_debugObstacle);
    }
    return mnavDrawResult(buffer);
}
