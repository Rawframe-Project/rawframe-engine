#pragma once

// The components 3D physics reads and writes (SPEC-0037), as physics2d's are
// for 2D: an entity with a Body3D, a Pose3D, and a Velocity3D has a body in
// the physics world; gameplay reads the committed pose and velocity, and
// changes the body by writing them, which the next step takes as a teleport
// (the body made again there) or a new velocity, or by writing an
// Impulse3D, which the next step applies once and clears. A body whose
// entity also has a Contact3D is told after each step what it touches and
// what overlaps it. Meters, seconds, kilograms, and radians; y is up
// (ADR-0046).
//
// Layouts are fixed and plain, so a script declares the same structs (the
// Kest ones are rawframe.physics3d's) and each field table below says what
// a matching declaration holds.

#include "rawframe/physics/layout.h"
#include "rawframe/physics/motion.h"
#include "rawframe/schema/stable_id.h"
#include "rawframe/world/entity.h"

#include <cstdint>
#include <span>
#include <string_view>

namespace rawframe::physics3d {

enum class Shape : std::uint8_t {
    /// `width` is the radius.
    Sphere = 0,
    /// `width`, `height`, and `depth` are half the sides along x, y, and z.
    Box = 1,
    /// Upright: `width` is the radius and `height` half the distance
    /// between the two centers.
    Capsule = 2,
    /// Upright: `width` is the radius and `height` half the height; a prism
    /// of sixteen sides.
    Cylinder = 3,
};

/// How the body is made. Changing any of it makes the body again, from the
/// current pose and velocity.
struct Body3D {
    static constexpr schema::ComponentTypeId kComponentTypeId =
        schema::ComponentTypeId::fromText("7629734c-ff2d-4081-8a30-0be42f65b566");
    static constexpr std::string_view kComponentName = "rawframe.physics3d.body";

    std::uint8_t motion = 0;
    std::uint8_t shape = 0;
    /// Turns about no axis: an upright character, a crate that must not tip.
    bool fixedRotation = false;
    /// Continuous collision against dynamic bodies too, for the fast.
    bool bullet = false;
    /// Detects what overlaps it and pushes nothing (a trigger zone).
    bool sensor = false;
    /// Its collision class's durable identity (rawframe.physics); nought is
    /// none.
    std::uint64_t collisionClass = 0;
    float width = 0;
    float height = 0;
    float depth = 0;
    /// Kilograms a cubic meter; nought is one.
    float density = 0;
    float friction = 0;
    float restitution = 0;
    float linearDamping = 0;
    float angularDamping = 0;
};

/// Where the body is: its origin, and its rotation as a unit quaternion. A
/// rotation of all noughts is taken as none.
struct Pose3D {
    static constexpr schema::ComponentTypeId kComponentTypeId =
        schema::ComponentTypeId::fromText("f6e10233-61bd-47ac-a8ba-1306428c84cd");
    static constexpr std::string_view kComponentName = "rawframe.physics3d.pose";

    double x = 0;
    double y = 0;
    double z = 0;
    float qx = 0;
    float qy = 0;
    float qz = 0;
    float qw = 0;
};

/// Meters a second, and radians a second about each world axis.
struct Velocity3D {
    static constexpr schema::ComponentTypeId kComponentTypeId =
        schema::ComponentTypeId::fromText("41a43405-0551-42d3-8fa2-a07054a3108a");
    static constexpr std::string_view kComponentName = "rawframe.physics3d.velocity";

    float x = 0;
    float y = 0;
    float z = 0;
    float angularX = 0;
    float angularY = 0;
    float angularZ = 0;
};

/// Newton seconds through the center of mass, and an angular impulse, for
/// the next step only.
struct Impulse3D {
    static constexpr schema::ComponentTypeId kComponentTypeId =
        schema::ComponentTypeId::fromText("1ba8257a-7c20-4e86-84b7-0df30972c3b9");
    static constexpr std::string_view kComponentName = "rawframe.physics3d.impulse";

    float x = 0;
    float y = 0;
    float z = 0;
    float angularX = 0;
    float angularY = 0;
    float angularZ = 0;
};

/// What a body touched and what overlapped it, written by every step: counts
/// now and for this step; the other body of the hardest hit this step, its
/// closing speed, and the normal from this body toward it; and the first
/// body that began to overlap it this step. An entity field is the null
/// entity when nothing began. Overlaps are counted on both sides: on the
/// sensor and on what is inside it.
struct Contact3D {
    static constexpr schema::ComponentTypeId kComponentTypeId =
        schema::ComponentTypeId::fromText("f8cec8db-03a1-4289-9748-6e82f36e778b");
    static constexpr std::string_view kComponentName = "rawframe.physics3d.contact";

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
    float hitNormalZ = 0;
    world::EntityHandle visitor;
};

/// What a ray found: the closest body along it, where, the surface's normal
/// there, and how far along the ray (0 at its origin, 1 at its end). A ray
/// meets only surfaces it arrives at from outside: one that starts inside a
/// body passes out of it unaware. A ray cast back in time hits a body
/// `discontinuous` when the body has no unbroken trail back to then, and so
/// was tried where it is now. Not a component: a value a query answers.
struct RayHit3D {
    bool hit = false;
    bool discontinuous = false;
    world::EntityHandle entity;
    double x = 0;
    double y = 0;
    double z = 0;
    float normalX = 0;
    float normalY = 0;
    float normalZ = 0;
    float fraction = 0;
};

/// SPEC-0037's character controller in three dimensions, as physics2d's
/// Character2D in two: an entity with a Body3D (kinematic, an upright
/// capsule) and a Character3D is moved by trace and slide. Gameplay writes
/// the Velocity3D it wants this tick, gravity and all; the step moves the
/// body as far along it as the world allows, sliding along what it meets,
/// and leaves in Velocity3D the velocity it moved with. The world's solid
/// bodies stop it, by its Body3D's collision class; other characters never
/// do. Numbers only, so it replicates and a client predicts it; a ray down
/// finds the entity underfoot. Its layout has padding after `ground`.
struct Character3D {
    static constexpr schema::ComponentTypeId kComponentTypeId =
        schema::ComponentTypeId::fromText("86a75d11-95b0-42d6-af49-cabc15fdf340");
    static constexpr std::string_view kComponentName = "rawframe.physics3d.character";

    /// The least upward a surface's normal may be and still be ground: its
    /// y, from 0 to 1 (0.7 stands on slopes up to about 45 degrees).
    float groundNormal = 0;
    /// A character that was on ground and now is not is stepped down to
    /// ground within this far below it; nought never.
    float snap = 0;
    /// Written by every step: the physics::Ground it is on, and the normal of
    /// what it stands on or slides along (nought in the air).
    std::uint8_t ground = 0;
    float groundNormalX = 0;
    float groundNormalY = 0;
    float groundNormalZ = 0;
};

/// Body3D, Pose3D, Velocity3D, Impulse3D, Contact3D, and Character3D, in
/// that order. An entity field appears as its two parts, `<name>.slot` and
/// `<name>.generation`.
[[nodiscard]] std::span<const physics::ComponentLayout> componentLayouts() noexcept;

/// RayHit3D as a script must declare it; its identity is none.
[[nodiscard]] const physics::ComponentLayout& rayHitLayout() noexcept;

} // namespace rawframe::physics3d
