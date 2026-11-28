#pragma once

#include <cstdint>

namespace rawframe::physics {

/// The per-axis joint vocabulary, the same in both
/// dimensions: how a joint holds one axis of its frame.
enum class JointAxis : std::uint8_t {
    Locked = 0,
    Free = 1,
    /// Free between the axis's lower and upper limit.
    Limited = 2,
};

/// What kind of joint a Joint3D or a Joint2D is, the same in both
/// dimensions. Each kind reads the fields it names and leaves the rest.
enum class JointKind : std::uint8_t {
    /// Each axis of its frame locked, free, or limited (JointAxis), with one
    /// motor: fixed, hinge, slider, and ball joints.
    Generic = 0,
    /// The anchors kept from `minLength` to `maxLength` apart: a rod when the
    /// two are equal, a rope when they are not. With a spring (`springHertz`
    /// above nought) it pulls toward `springLength` inside that range.
    Distance = 1,
    /// A wheel on a car: `b` slides along `a`'s suspension axis on a spring
    /// (`springHertz`) back toward where it was made, from `minLength` to
    /// `maxLength` from there when they differ, and spins about its axle,
    /// which the motor may drive.
    Wheel = 2,
    /// A rope from each anchor up over a fixed point in the world
    /// (`groundA`, `groundB`): the A side's length plus `ratio` times the B
    /// side's stays what it was when the joint was made.
    Pulley = 3,
    /// Couples the two bodies' spins: a's turn plus `ratio` times b's stays
    /// what it was when made (positive for meshed cogs, which turn opposite
    /// ways). Both bodies must turn on hinges to one common frame (a chassis,
    /// the ground); the gear holds nothing else and adds nothing to that
    /// frame.
    Gear = 4,
};

} // namespace rawframe::physics
