#pragma once

// Avoidance (ADR-0056, D411): agents steering round each other by velocity
// obstacles (ORCA), over Maul Nav's own. It works on the ground plane, a 3D
// World's (x, z) or a 2D World's (x, y), in meters and seconds, and needs no
// navmesh: a navmesh says where to go, avoidance how to get by on the way.
// The same agents give the same velocities in any order, on every target.

#include "rawframe/result/result.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>

namespace rawframe::navigation {

/// A point or a velocity on the ground plane.
struct Ground {
    double x = 0;
    double y = 0;
};

/// An agent as the World has it this step.
struct AvoidingAgent {
    Ground position;
    /// Its velocity now, as avoidance last gave it.
    Ground velocity;
    /// Where it would go, in meters a second.
    Ground preferred;
    /// More than nought.
    double radius = 0.5;
    /// At least nought.
    double maxSpeed = 1;
    /// More than nought: of each avoidance between two agents, one takes the
    /// other's priority over the sum of both.
    double priority = 1;
    /// Orders neighbours at equal distances; ids should differ.
    std::uint64_t id = 0;
};

struct AvoidanceSettings {
    /// Agents at most, 1 to 1,048,576.
    std::size_t maximumAgents = 4096;
    /// Neighbours each agent avoids, the nearest first, 1 to 256.
    std::size_t neighbours = 10;
    /// How far each agent looks for neighbours, in meters.
    double neighbourDistance = 10;
    /// How far ahead agents avoid each other, in seconds.
    double timeHorizon = 2;
};

class Avoidance {
public:
    /// Refuses (`InvalidArgument`) settings out of range.
    [[nodiscard]] static result::Result<std::unique_ptr<Avoidance>> create(const AvoidanceSettings& settings);

    Avoidance(const Avoidance&) = delete;
    Avoidance& operator=(const Avoidance&) = delete;
    ~Avoidance();

    /// Each agent's velocity for a step of `seconds`, in `velocities`, the
    /// agents' order: the one nearest its preferred velocity, no faster than
    /// its maximum, that keeps clear of the others within the time horizon
    /// if they do their share. Refuses (`InvalidArgument`) an agent out of
    /// range or not finite, and (`ResourceExhausted`) more agents than the
    /// settings allow.
    [[nodiscard]] result::Status
    avoid(std::span<const AvoidingAgent> agents, double seconds, std::span<Ground> velocities);

    struct State;

private:
    explicit Avoidance(std::unique_ptr<State> state) noexcept;
    std::unique_ptr<State> state_;
};

} // namespace rawframe::navigation
