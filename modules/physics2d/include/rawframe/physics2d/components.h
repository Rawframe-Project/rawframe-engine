#pragma once

// The components 2D physics reads and writes (SPEC-0037). An entity with a
// Body2D, a Pose2D, and a Velocity2D has a body in the physics world; gameplay
// reads the committed pose and velocity, and changes the body by writing
// them, which the next step takes as a teleport (the body made again there)
// or a new velocity, or by
// writing an Impulse2D, which the next step applies once and clears. A body
// whose entity also has a Contact2D is told after each step what it touches
// and what overlaps it (SPEC-0037's per-body opt-in). Units are meters,
// seconds, kilograms, and radians (SPEC-0037's 2D meter).
//
// Layouts are fixed and plain, so a script declares the same structs (the
// Kest ones are rawframe.physics2d's) and each field table below says what
// a matching declaration holds.

#include "rawframe/schema/stable_id.h"
#include "rawframe/world/entity.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace rawframe::physics2d {

enum class Motion : std::uint8_t {
    Static = 0,
    Kinematic = 1,
    Dynamic = 2,
};

enum class Shape : std::uint8_t {
    /// `width` is the radius.
    Circle = 0,
    /// `width` and `height` are half the sides.
    Box = 1,
    /// Upright: `width` is the radius and `height` half the distance
    /// between the two centers.
    Capsule = 2,
};

/// How the body is made. Changing any of it makes the body again, from the
/// current pose and velocity.
struct Body2D {
    static constexpr schema::ComponentTypeId kComponentTypeId =
        schema::ComponentTypeId::fromText("d0ae39a2-4803-4ee0-9d45-1980875324d0");
    static constexpr std::string_view kComponentName = "rawframe.physics2d.body";

    std::uint8_t motion = 0;
    std::uint8_t shape = 0;
    bool fixedRotation = false;
    /// Continuous collision against other bodies, for the fast.
    bool bullet = false;
    /// Detects what overlaps it and pushes nothing (a trigger zone).
    bool sensor = false;
    /// Its collision class's durable identity (physics.h); nought is none.
    std::uint64_t collisionClass = 0;
    float width = 0;
    float height = 0;
    float density = 0;
    float friction = 0;
    float restitution = 0;
    float linearDamping = 0;
    float angularDamping = 0;
};

/// Where the body is: its origin, and its rotation as a unit complex number
/// (cosine and sine). A rotation of (0, 0) is taken as none.
struct Pose2D {
    static constexpr schema::ComponentTypeId kComponentTypeId =
        schema::ComponentTypeId::fromText("fdf0050d-881c-4451-b541-754c67d1bbf8");
    static constexpr std::string_view kComponentName = "rawframe.physics2d.pose";

    double x = 0;
    double y = 0;
    float c = 0;
    float s = 0;
};

/// Meters a second, and radians a second.
struct Velocity2D {
    static constexpr schema::ComponentTypeId kComponentTypeId =
        schema::ComponentTypeId::fromText("c87b8379-f398-4de8-8e8e-0eb69124c5f8");
    static constexpr std::string_view kComponentName = "rawframe.physics2d.velocity";

    float x = 0;
    float y = 0;
    float angular = 0;
};

/// Newton seconds through the center of mass, and an angular impulse, for
/// the next step only.
struct Impulse2D {
    static constexpr schema::ComponentTypeId kComponentTypeId =
        schema::ComponentTypeId::fromText("4df62be9-e5a3-4a65-8c89-b8690b6bd079");
    static constexpr std::string_view kComponentName = "rawframe.physics2d.impulse";

    float x = 0;
    float y = 0;
    float angular = 0;
};

/// What a body touched and what overlapped it, written by every step: counts
/// now and for this step, the other body of the hardest contact begun this
/// step (its closing speed, and the normal from this body toward it), and
/// the first body that began to overlap it this step. An entity field is
/// the null entity when nothing began. Overlaps are counted on both sides:
/// on the sensor and on what is inside it.
struct Contact2D {
    static constexpr schema::ComponentTypeId kComponentTypeId =
        schema::ComponentTypeId::fromText("1f84d21a-eff7-400e-95f0-a45d96deaa07");
    static constexpr std::string_view kComponentName = "rawframe.physics2d.contact";

    std::uint32_t touching = 0;
    std::uint32_t began = 0;
    std::uint32_t ended = 0;
    std::uint32_t overlapping = 0;
    std::uint32_t entered = 0;
    std::uint32_t exited = 0;
    world::EntityHandle hit;
    float hitSpeed = 0;
    float hitNormalX = 0;
    float hitNormalY = 0;
    world::EntityHandle visitor;
};

/// What a ray found: the closest body along it, where, the surface's normal
/// there, and how far along the ray (0 at its origin, 1 at its end). A ray
/// that starts inside a body hits it `inside`, at its origin, with no normal.
/// A ray cast back in time hits a body `discontinuous` when the body has no
/// unbroken trail back to then (made or teleported since), and so was tried
/// where it is now. Not a component: a value a query answers.
struct RayHit2D {
    bool hit = false;
    bool inside = false;
    bool discontinuous = false;
    world::EntityHandle entity;
    double x = 0;
    double y = 0;
    float normalX = 0;
    float normalY = 0;
    float fraction = 0;
};

enum class FieldType : std::uint8_t {
    U8,
    U32,
    U64,
    Bool,
    F32,
    F64,
};

struct ComponentField {
    std::string_view name;
    std::size_t offset = 0;
    FieldType type = FieldType::U8;
};

/// One physics component as a script must declare it.
struct ComponentLayout {
    schema::ComponentTypeId id;
    std::string_view name;
    /// The type's name in the script.
    std::string_view scriptType;
    std::size_t size = 0;
    std::size_t alignment = 0;
    std::span<const ComponentField> fields;
};

/// Body2D, Pose2D, Velocity2D, Impulse2D, and Contact2D, in that order.
/// An entity field appears as its two parts, `<name>.slot` and
/// `<name>.generation`.
[[nodiscard]] std::span<const ComponentLayout> componentLayouts() noexcept;

/// RayHit2D as a script must declare it; its identity is none.
[[nodiscard]] const ComponentLayout& rayHitLayout() noexcept;

} // namespace rawframe::physics2d
