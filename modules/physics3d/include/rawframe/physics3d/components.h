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

#include "rawframe/physics/joint.h"
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
    /// Static only: the triangles of the mesh its entity's Mesh3D names, at
    /// the body's pose, facing out counter-clockwise. `width`, `height`,
    /// and `depth` are unused, and it is never a sensor.
    Mesh = 4,
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

/// What a ray or a sweep found: the closest body along it, where, the
/// surface's normal there, and how far along (0 at its origin, 1 at its
/// end). A ray meets only surfaces it arrives at from outside: one that
/// starts inside a body passes out of it unaware. A sweep that starts inside
/// a body meets it `inside`, at its start, with no normal. A ray cast back in
/// time hits a body `discontinuous` when the body has no unbroken trail back
/// to then, and so was tried where it is now. Not a component: a value a
/// query answers.
struct RayHit3D {
    bool hit = false;
    bool inside = false;
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

/// Which of the physics settings' meshes a body of Shape::Mesh is made of,
/// by the identity the game gives it. Changing it makes the body again.
struct Mesh3D {
    static constexpr schema::ComponentTypeId kComponentTypeId =
        schema::ComponentTypeId::fromText("d8f9e868-d5ee-4230-9379-0abeab4018b7");
    static constexpr std::string_view kComponentName = "rawframe.physics3d.mesh";

    std::uint64_t mesh = 0;
};

/// SPEC-0037's joint (section 10): an entity with a Joint3D holds two
/// bodies' entities together, per axis of the joint's frame. The frame sits
/// at each body's anchor, its z along the body's axis (all noughts is +z),
/// and each of its three linear and three angular axes is locked, free, or
/// limited, in meters and radians. Fixed, hinge, slider, and ball joints are
/// settings of these (rawframe.physics3d's `fixed`, `hinge`, `slider`, and
/// `ball`). One motor may drive one axis that moves, at `motorSpeed` with at
/// most `motorEffort`. At most one angular axis may be limited, and then
/// the other two are both locked or both free. At least one of the bodies
/// is dynamic. A joint that cannot be made (a body without one, values out
/// of range) waits until it or its bodies change. Changing it, or remaking
/// either body, makes it again. Past `breakForce` newtons or `breakTorque`
/// newton meters of reaction (nought: never) it breaks: the step writes
/// `broken`, and a broken joint stays unmade until gameplay writes `broken`
/// false.
struct Joint3D {
    static constexpr schema::ComponentTypeId kComponentTypeId =
        schema::ComponentTypeId::fromText("963bf147-4afe-4047-9b33-bdf528bf026a");
    static constexpr std::string_view kComponentName = "rawframe.physics3d.joint";

    world::EntityHandle a;
    world::EntityHandle b;
    float anchorAX = 0;
    float anchorAY = 0;
    float anchorAZ = 0;
    float anchorBX = 0;
    float anchorBY = 0;
    float anchorBZ = 0;
    float axisAX = 0;
    float axisAY = 0;
    float axisAZ = 0;
    float axisBX = 0;
    float axisBY = 0;
    float axisBZ = 0;
    float linearLowerX = 0;
    float linearLowerY = 0;
    float linearLowerZ = 0;
    float linearUpperX = 0;
    float linearUpperY = 0;
    float linearUpperZ = 0;
    float angularLowerX = 0;
    float angularLowerY = 0;
    float angularLowerZ = 0;
    float angularUpperX = 0;
    float angularUpperY = 0;
    float angularUpperZ = 0;
    float motorSpeed = 0;
    float motorEffort = 0;
    float breakForce = 0;
    float breakTorque = 0;
    /// physics::JointAxis each.
    std::uint8_t linearX = 0;
    std::uint8_t linearY = 0;
    std::uint8_t linearZ = 0;
    std::uint8_t angularX = 0;
    std::uint8_t angularY = 0;
    std::uint8_t angularZ = 0;
    /// Nought for none; 1 to 3 drive linear x, y, z, and 4 to 6 angular.
    std::uint8_t motor = 0;
    /// Whether the two bodies still collide with each other.
    bool collideConnected = false;
    bool broken = false;
};

/// Body3D, Pose3D, Velocity3D, Impulse3D, Contact3D, Character3D, Mesh3D,
/// and Joint3D, in that order. An entity field appears as its two parts, `<name>.slot` and
/// `<name>.generation`.
[[nodiscard]] std::span<const physics::ComponentLayout> componentLayouts() noexcept;

/// What an overlap query found: how many bodies overlap the shape asked
/// about, and the one at the asked index among them in entity order (the
/// null entity past the last). Not a component: a value a query answers.
struct Overlap3D {
    std::uint32_t count = 0;
    world::EntityHandle entity;
};

/// RayHit3D and Overlap3D as a script must declare them; their identities
/// are none.
[[nodiscard]] std::span<const physics::ComponentLayout> answerLayouts() noexcept;

} // namespace rawframe::physics3d
