// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Avoidance in space (mnav-0006): each flier's ORCA planes against the
// spheres near it and its nearest neighbours (RVO2-3D src/Agent.cc,
// computeNewVelocity, in binary64), with the ground call's share by
// priority, sorted grid and sidestep.

#include "avoidance.h"
#include "crowd.h"
#include "obstacle.h"
#include "orca3.h"

#include "maul-nav/avoidance.h"
#include "maul-nav/base.h"

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

// The spheres an avoidance call is given.
typedef struct Spheres
{
    const mnavSphere* items;
    int32_t count;
} Spheres;

static bool NearBefore(const Spheres* spheres, mnavObstacleNear a, mnavObstacleNear b)
{
    if (a.distance != b.distance)
    {
        return a.distance < b.distance;
    }
    uint64_t ia = spheres->items[a.vertex].id;
    uint64_t ib = spheres->items[b.vertex].id;
    return ia != ib ? ia < ib : a.vertex < b.vertex;
}

// The spheres an agent may meet within the horizon, the nearest first,
// up to the limit, into list; returns how many. Each is looked at: a
// call's spheres are the few moving things among fliers.
static int32_t NearSpheres(const mnavAgent3D* agent, const Spheres* spheres, double horizon,
                           mnavObstacleNear* list, int32_t limit)
{
    int32_t count = 0;
    for (int32_t s = 0; s < spheres->count; ++s)
    {
        const mnavSphere* sphere = &spheres->items[s];
        double speed = sqrt(mnavDot3(sphere->velocity, sphere->velocity));
        double range = horizon * (agent->maxSpeed + speed) + agent->radius;
        mnavPos3 rel = mnavSub3(sphere->center, agent->position);
        double gap = sqrt(mnavDot3(rel, rel)) - sphere->radius;
        gap = gap > 0.0 ? gap : 0.0;
        if (gap >= range)
        {
            continue;
        }
        mnavObstacleNear candidate = {gap * gap, s};
        if (count == limit && !NearBefore(spheres, candidate, list[count - 1]))
        {
            continue;
        }
        int32_t i = count < limit ? count++ : count - 1;
        while (i > 0 && NearBefore(spheres, candidate, list[i - 1]))
        {
            list[i] = list[i - 1];
            i -= 1;
        }
        list[i] = candidate;
    }
    return count;
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
static mnavPos3 Solve(mnavAvoidance* a, const mnavAgent3D* agents, const Spheres* spheres,
                      double step, int32_t i)
{
    const mnavAgent3D* self = &agents[i];
    int32_t near = NearSpheres(self, spheres, a->def.obstacleTimeHorizon, a->near,
                               a->def.limits.obstacleNeighbors);
    int32_t count = 0;
    for (int32_t k = 0; k < near; ++k)
    {
        a->planes[count++] =
            SpherePlane(self, &spheres->items[a->near[k].vertex], a->def.obstacleTimeHorizon, step);
    }
    int32_t fixed = count;
    int32_t neighbors = mnavCrowdNeighbors(&a->crowd, self->position, i);
    for (int32_t n = 0; n < neighbors; ++n)
    {
        const mnavAgent3D* other = &agents[a->crowd.neighbors[n].index];
        a->planes[count++] = mnavPairPlane(self->position, self->velocity, other->position,
                                           other->velocity, self->radius + other->radius,
                                           other->priority / (self->priority + other->priority),
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
    crowd->space = true;
    for (int32_t i = 0; i < agentCount; ++i)
    {
        if (!GoodAgent(&agents[i]))
        {
            return mnav_errorInvalid;
        }
        crowd->keys[i] = mnavCrowdKeyOf(crowd, agents[i].position, agents[i].id, i);
    }
    mnavSortCrowd(crowd, agentCount);
    Spheres given = {spheres, sphereCount};
    for (int32_t i = 0; i < agentCount; ++i)
    {
        velocitiesOut[i] = Solve(avoidance, agents, &given, step, i);
    }
    return mnav_success;
}
