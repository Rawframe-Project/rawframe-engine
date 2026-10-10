// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Avoidance (mnav-0006): local steering among agents by velocity
// obstacles (ORCA), apart from the navmesh: nothing here uses a navmesh,
// and a host may use it without one. It works on the ground plane, a 3D
// world's (x, z) or a 2D world's (x, y), and for fliers in space, in
// meters and seconds.

#ifndef MAUL_NAV_AVOIDANCE_H
#define MAUL_NAV_AVOIDANCE_H

#include "maul-nav/base.h"
#include "maul-nav/draw.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

// The most agents and neighbours per agent an avoidance set may have.
#define MNAV_MAX_AVOIDANCE_AGENTS    1048576
#define MNAV_MAX_AVOIDANCE_NEIGHBORS 256
// The most obstacle points one call may have.
#define MNAV_MAX_AVOIDANCE_VERTICES 1048576
// The ranges of avoidance input, which keep its arithmetic accurate and
// its grid cells within 64-bit integers: each coordinate within this
// many meters of the origin, each velocity component and maximum speed
// within this many meters per second, radii up to this many meters, and
// the step and horizons this many seconds or longer.
#define MNAV_MAX_AVOIDANCE_COORDINATE 1.0e12
#define MNAV_MAX_AVOIDANCE_SPEED      1.0e6
#define MNAV_MAX_AVOIDANCE_RADIUS     1.0e6
#define MNAV_MIN_AVOIDANCE_TIME       1.0e-6

    // A point or a velocity on the ground plane, binary64.
    typedef struct mnavPos2
    {
        double x;
        double y;
    } mnavPos2;

    // An agent as the host sees it this step, its values in the ranges
    // above.
    typedef struct mnavAgent
    {
        mnavPos2 position;
        mnavPos2 velocity;
        // Where the agent would go, in meters per second.
        mnavPos2 preferred;
        // More than 0, up to MNAV_MAX_AVOIDANCE_RADIUS.
        double radius;
        // 0 to MNAV_MAX_AVOIDANCE_SPEED.
        double maxSpeed;
        // More than 0: of each avoidance between two agents, one takes
        // the other's priority over the sum of both.
        double priority;
        // The host's id for the agent, which orders neighbours at equal
        // distances; ids should differ, or the input's order counts.
        uint64_t id;
        // The groups the agent belongs to, as bits; 0 for none.
        uint32_t layers;
        // The groups the agent passes through: it does not avoid an agent
        // or obstacle whose layers share a bit with these, and takes the
        // whole avoidance of an agent that ignores it. 0 avoids all.
        uint32_t ignores;
    } mnavAgent;

    // An obstacle agents steer round, taking the whole avoidance: one point
    // and a radius (a circle), two points (a segment), or three or more
    // counterclockwise (a polygon agents stay out of). A moving one
    // translates at its velocity; it does not turn.
    typedef struct mnavObstacle
    {
        // The points, in meters. Only read during the call.
        const mnavPos2* points;
        // At least 1.
        int32_t pointCount;
        // A circle's radius, more than 0 and up to
        // MNAV_MAX_AVOIDANCE_RADIUS; 0 for two or more points.
        double radius;
        // Each component within MNAV_MAX_AVOIDANCE_SPEED.
        mnavPos2 velocity;
        // The host's id for the obstacle, which orders obstacles at equal
        // distances; ids should differ, or the input's order counts.
        uint64_t id;
        // The groups the obstacle belongs to, as an agent's layers.
        uint32_t layers;
    } mnavObstacle;

    // An agent in space, for mnavAvoid3D, its values in the ranges above.
    typedef struct mnavAgent3D
    {
        mnavPos3 position;
        mnavPos3 velocity;
        // Where the agent would go, in meters per second.
        mnavPos3 preferred;
        // More than 0, up to MNAV_MAX_AVOIDANCE_RADIUS.
        double radius;
        // 0 to MNAV_MAX_AVOIDANCE_SPEED.
        double maxSpeed;
        // More than 0, as mnavAgent's.
        double priority;
        // The host's id for the agent, as mnavAgent's.
        uint64_t id;
        // The groups the agent belongs to and passes through, as
        // mnavAgent's.
        uint32_t layers;
        uint32_t ignores;
    } mnavAgent3D;

    // A sphere fliers steer round, taking the whole avoidance, as a circle
    // obstacle on the ground. A moving one moves at its velocity.
    typedef struct mnavSphere
    {
        mnavPos3 center;
        // More than 0, up to MNAV_MAX_AVOIDANCE_RADIUS.
        double radius;
        // Each component within MNAV_MAX_AVOIDANCE_SPEED.
        mnavPos3 velocity;
        // The host's id for the sphere, which orders spheres at equal
        // distances; ids should differ, or the input's order counts.
        uint64_t id;
        // The groups the sphere belongs to, as an agent's layers.
        uint32_t layers;
    } mnavSphere;

    // The limits of an avoidance set.
    typedef struct mnavAvoidanceLimits
    {
        // Agents in one call, 1 to MNAV_MAX_AVOIDANCE_AGENTS.
        int32_t agents;
        // Neighbours each agent avoids, the nearest first, 1 to
        // MNAV_MAX_AVOIDANCE_NEIGHBORS.
        int32_t neighbors;
        // Obstacle points in one call, a sphere counting as one, 0 to
        // MNAV_MAX_AVOIDANCE_VERTICES.
        int32_t obstacleVertices;
        // Obstacle edges, circles or spheres each agent avoids, the nearest
        // first, 1 to MNAV_MAX_AVOIDANCE_NEIGHBORS.
        int32_t obstacleNeighbors;
    } mnavAvoidanceLimits;

    // How an avoidance set is made. Build it with mnavDefaultAvoidanceDef.
    typedef struct mnavAvoidanceDef
    {
        uint32_t cookie;
        // MNAV_ABI_VERSION, set by mnavDefaultAvoidanceDef.
        uint32_t version;
        // The allocator the set uses; zeroed for the C library's.
        mnavAllocator allocator;
        mnavAvoidanceLimits limits;
        // How far away, center to center, another agent counts as a
        // neighbour, in meters, more than 0.
        double neighborDistance;
        // How far ahead agents avoid each other, in seconds, at least
        // MNAV_MIN_AVOIDANCE_TIME.
        double timeHorizon;
        // How far ahead agents avoid obstacles, in seconds, at least
        // MNAV_MIN_AVOIDANCE_TIME.
        double obstacleTimeHorizon;
    } mnavAvoidanceDef;

    // An avoidance set: the memory its limits need.
    typedef struct mnavAvoidance mnavAvoidance;

// The cookie mnavDefaultAvoidanceDef sets and mnavCreateAvoidance checks.
#define MNAV_AVOIDANCE_DEF_COOKIE 0x4E415641u

    /// Returns the default avoidance def: up to 4096 agents, each avoiding
    /// its 10 nearest neighbours within 10 m, 2 s ahead, and up to 4096
    /// obstacle points, each agent avoiding its 16 nearest obstacle edges
    /// or circles, 2 s ahead.
    /// Built in the program, so that it carries the version of the
    /// headers the program includes.
    ///
    /// @return The def.
    /// @par Thread safety
    /// Safe from any thread.
    static inline mnavAvoidanceDef mnavDefaultAvoidanceDef(void)
    {
#ifdef __cplusplus
        mnavAvoidanceDef def = {};
#else
    mnavAvoidanceDef def = {0};
#endif
        def.cookie = MNAV_AVOIDANCE_DEF_COOKIE;
        def.version = MNAV_ABI_VERSION;
        def.limits.agents = 4096;
        def.limits.neighbors = 10;
        def.limits.obstacleVertices = 4096;
        def.limits.obstacleNeighbors = 16;
        def.neighborDistance = 10.0;
        def.timeHorizon = 2.0;
        def.obstacleTimeHorizon = 2.0;
        return def;
    }

    /// Makes an avoidance set with the memory its limits need.
    ///
    /// @param def         The def, from mnavDefaultAvoidanceDef.
    /// @param avoidanceOut Receives the set, or NULL on failure.
    /// @return `mnav_success`; `mnav_errorInvalid` for a NULL argument or a def
    /// not from mnavDefaultAvoidanceDef; `mnav_errorVersion` for one built
    /// against another major or minor version; `mnav_errorRange` for a limit,
    /// distance or horizon out of its range; `mnav_errorCapacity` when the
    /// allocator fails.
    /// @par Thread safety
    /// Safe from any thread.
    MNAV_NODISCARD MNAV_API mnavResult mnavCreateAvoidance(const mnavAvoidanceDef* def,
                                                           mnavAvoidance** avoidanceOut);

    /// Destroys an avoidance set.
    ///
    /// @param avoidance The set, or NULL.
    /// @par Thread safety
    /// Safe from any thread; the set is used by one thread at a time.
    MNAV_API void mnavDestroyAvoidance(mnavAvoidance* avoidance);

    /// Finds each agent's new velocity: the one nearest its preferred
    /// velocity, no faster than its maximum speed, that avoids the
    /// obstacles within the obstacle horizon and colliding with its
    /// neighbours within the time horizon if they do their share (ORCA);
    /// when none does, the one that keeps clear of the obstacles and breaks
    /// the agents' constraints least. An agent sees an obstacle edge only
    /// from outside it.
    /// An agent held back from its preferred velocity aims 1% of its speed
    /// to the right of it, so that agents meeting in perfect symmetry pass
    /// rather than stop face to face. The same agents give the same
    /// velocities in any order, on every platform.
    ///
    /// @param avoidance     The set.
    /// @param agents        The agents.
    /// @param agentCount    How many, at least 0.
    /// @param obstacles     The obstacles.
    /// @param obstacleCount How many, at least 0.
    /// @param step          The step the velocities are for, in seconds,
    ///                      at least MNAV_MIN_AVOIDANCE_TIME: agents
    ///                      already overlapping part
    ///                      within it.
    /// @param velocitiesOut Receives agentCount velocities, in the agents'
    ///                      order.
    /// @return `mnav_success`; `mnav_errorInvalid` for a NULL argument with
    /// agents or obstacles, a negative count, a step not finite or shorter
    /// than MNAV_MIN_AVOIDANCE_TIME, an agent or obstacle with a value not
    /// finite or out of
    /// its range, a zero-length obstacle edge, a circle's radius not more
    /// than 0, another obstacle with a radius, or a polygon not
    /// counterclockwise; `mnav_errorLimit` for more agents or obstacle
    /// points than the set's limits.
    /// @par Thread safety
    /// Safe from any thread; the set is used by one thread at a time.
    MNAV_NODISCARD MNAV_API mnavResult mnavAvoid(mnavAvoidance* avoidance, const mnavAgent* agents,
                                                 int32_t agentCount, const mnavObstacle* obstacles,
                                                 int32_t obstacleCount, double step,
                                                 mnavPos2* velocitiesOut);

    /// Finds each flier's new velocity, as mnavAvoid does on the ground:
    /// the one nearest its preferred velocity, no faster than its maximum
    /// speed, that avoids the spheres within the obstacle horizon and
    /// colliding with its neighbours within the time horizon if they do
    /// their share (ORCA in space); when none does, the one that keeps
    /// clear of the spheres and breaks the agents' constraints least.
    /// An agent held back from its preferred velocity aims 1% of its speed
    /// to the right of it, +Y being up; rising vertically it turns toward
    /// +X, falling toward -X.
    /// The same agents give the same velocities in any order, on every
    /// platform.
    ///
    /// @param avoidance     The set.
    /// @param agents        The agents.
    /// @param agentCount    How many, at least 0.
    /// @param spheres       The spheres.
    /// @param sphereCount   How many, at least 0.
    /// @param step          The step the velocities are for, in seconds,
    ///                      at least MNAV_MIN_AVOIDANCE_TIME: agents
    ///                      already overlapping part within it.
    /// @param velocitiesOut Receives agentCount velocities, in the agents'
    ///                      order.
    /// @return `mnav_success`; `mnav_errorInvalid` for a NULL argument with
    /// agents or spheres, a negative count, a step not finite or shorter
    /// than MNAV_MIN_AVOIDANCE_TIME, or an agent or sphere with a value not
    /// finite or out of its range; `mnav_errorLimit` for more agents than
    /// the set's limit or more spheres than its obstacle points.
    /// @par Thread safety
    /// Safe from any thread; the set is used by one thread at a time.
    MNAV_NODISCARD MNAV_API mnavResult mnavAvoid3D(mnavAvoidance* avoidance,
                                                   const mnavAgent3D* agents, int32_t agentCount,
                                                   const mnavSphere* spheres, int32_t sphereCount,
                                                   double step, mnavPos3* velocitiesOut);

    /// Appends each agent's outline, a 16-gon (mnav_debugAgent), and a line
    /// to each neighbour mnavAvoid would give it (mnav_debugNeighbor), at a
    /// height, agent X and Y lying at ground X and Z.
    ///
    /// @param avoidance  The set; its scratch is used.
    /// @param agents     The agents.
    /// @param agentCount How many, at least 0.
    /// @param height     The lines' height.
    /// @param buffer     The buffer appended to.
    /// @return `mnav_success`; `mnav_errorInvalid` for a NULL argument with
    /// agents, an agent as mnavAvoid refuses it, a height not finite, or a
    /// buffer with a count out of range, an array missing or an origin not
    /// finite; `mnav_errorLimit` for more agents than the set's limit;
    /// `mnav_errorCapacity` when the buffer filled, its counts saying what
    /// the whole needs.
    /// @par Thread safety
    /// Safe from any thread; the set and the buffer are used by one thread
    /// at a time.
    MNAV_NODISCARD MNAV_API mnavResult mnavDebugAvoidance(mnavAvoidance* avoidance,
                                                          const mnavAgent* agents,
                                                          int32_t agentCount, double height,
                                                          mnavDebugBuffer* buffer);

    /// Appends each flier's outline, three 16-gons on its radius, one in
    /// each axis plane (mnav_debugAgent), a line to each neighbour
    /// mnavAvoid3D would give it (mnav_debugNeighbor), and each sphere's
    /// three rings (mnav_debugObstacle).
    ///
    /// @param avoidance   The set; its scratch is used.
    /// @param agents      The agents.
    /// @param agentCount  How many, at least 0.
    /// @param spheres     The spheres.
    /// @param sphereCount How many, at least 0.
    /// @param buffer      The buffer appended to.
    /// @return `mnav_success`; `mnav_errorInvalid` for a NULL argument with
    /// agents or spheres, a negative count, an agent or sphere as
    /// mnavAvoid3D refuses it, or a buffer with a count out of range, an
    /// array missing or an origin not finite; `mnav_errorLimit` for more
    /// agents or spheres than mnavAvoid3D takes; `mnav_errorCapacity` when
    /// the buffer filled, its counts saying what the whole needs.
    /// @par Thread safety
    /// Safe from any thread; the set and the buffer are used by one thread
    /// at a time.
    MNAV_NODISCARD MNAV_API mnavResult
    mnavDebugAvoidance3D(mnavAvoidance* avoidance, const mnavAgent3D* agents, int32_t agentCount,
                         const mnavSphere* spheres, int32_t sphereCount, mnavDebugBuffer* buffer);

#ifdef __cplusplus
}
#endif

#endif // MAUL_NAV_AVOIDANCE_H
