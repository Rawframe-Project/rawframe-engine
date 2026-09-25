#pragma once

// The components 2D physics reads and writes (SPEC-0037). An entity with a
// Body2D, a Pose2D, and a Velocity2D has a body in the physics world; gameplay
// reads the committed pose and velocity, and changes the body by writing
// them, which the next step takes as a teleport (the body made again there)
// or a new velocity; by writing an Impulse2D, which the next step applies
// once and clears; or, for a kinematic body, by setting a Target2D, which
// the next step moves it to. A body
// whose entity also has a Contact2D is told after each step what it touches
// and what overlaps it (SPEC-0037's per-body opt-in). Units are meters,
// seconds, kilograms, and radians (SPEC-0037's 2D meter).
//
// Layouts are fixed and plain, so a script declares the same structs (the
// Kest ones are rawframe.physics2d's) and each field table below says what
// a matching declaration holds.

#include "rawframe/physics/ground.h"
#include "rawframe/physics/joint.h"
#include "rawframe/physics/motion.h"
#include "rawframe/schema/layout.h"
#include "rawframe/schema/stable_id.h"
#include "rawframe/world/entity.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace rawframe::physics2d {

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

/// Where a kinematic body is to be after the next step (SPEC-0037's
/// kinematic target): the step chooses the velocities that carry it there,
/// so what it meets on the way is pushed, never jumped over as a teleport
/// would. Applied once while `set`, which the step clears; a body that is
/// not kinematic ignores it. A rotation of two noughts is none.
struct Target2D {
    static constexpr schema::ComponentTypeId kComponentTypeId =
        schema::ComponentTypeId::fromText("3c4ab1c1-49f4-4d96-8b26-ebe3e62e5271");
    static constexpr std::string_view kComponentName = "rawframe.physics2d.target";

    double x = 0;
    double y = 0;
    float c = 0;
    float s = 0;
    bool set = false;
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

/// SPEC-0037's character controller: an entity with a Body2D (kinematic,
/// an upright capsule) and a Character2D is moved by trace and slide rather
/// than by velocity alone. Gameplay writes the Velocity2D it wants this
/// tick, gravity and all; the step moves the body as far along it as the
/// world allows, sliding along what it meets, and leaves in Velocity2D the
/// velocity it moved with. The world's solid bodies stop it, by its Body2D's
/// collision class; other characters never do, and it pushes dynamic bodies
/// as a kinematic body does. Nothing about it is random or timed, so a
/// predicting client moves its own character exactly as the server does
/// wherever the same things are in its way. Its layout has padding after
/// `ground`.
struct Character2D {
    static constexpr schema::ComponentTypeId kComponentTypeId =
        schema::ComponentTypeId::fromText("edfb6a6b-7640-4204-9ff7-d1e07b4fcac8");
    static constexpr std::string_view kComponentName = "rawframe.physics2d.character";

    /// The least upward a surface's normal may be and still be ground: its
    /// y, from 0 to 1 (0.7 stands on slopes up to about 45 degrees).
    float groundNormal = 0;
    /// A character that was on ground and now is not is stepped down to
    /// ground within this far below it, so walking down slopes and stairs
    /// keeps it on them; nought never.
    float snap = 0;
    /// Written by every step: the Ground it is on, and the normal of what it
    /// stands on or slides along (nought in the air). Numbers only, so the
    /// component replicates and a client predicts it; a ray down finds the
    /// entity underfoot.
    std::uint8_t ground = 0;
    float groundNormalX = 0;
    float groundNormalY = 0;
};

/// SPEC-0037's joint (section 10) in two dimensions, physics3d's Joint3D
/// on the reduced axis set: an entity with a Joint2D holds two bodies'
/// entities together along the linear x and y and the angular axis of its
/// frame. The frame sits at each body's anchor, its x along `axis` in `a`'s
/// frame (all noughts is +x). Every axis is locked, free, or limited, in
/// meters and radians from where the bodies are when it is made. What it
/// can be is closed: all locked (a weld), only the angle moving (a hinge),
/// only one linear axis moving (a slider), or all free (the two only stop
/// colliding); any other is refused. One motor may drive the axis that
/// moves. At least one of the bodies is dynamic. A joint that cannot be
/// made waits until it or its bodies change; changing it, or making either
/// body again, makes it again. Past `breakForce` newtons or `breakTorque`
/// newton meters of reaction (nought: never) it breaks: the step writes
/// `broken`, and a broken joint stays unmade until gameplay writes `broken`
/// false.
struct Joint2D {
    static constexpr schema::ComponentTypeId kComponentTypeId =
        schema::ComponentTypeId::fromText("217eaf04-507b-4d94-b363-2c8bf99f4174");
    static constexpr std::string_view kComponentName = "rawframe.physics2d.joint";

    world::EntityHandle a;
    world::EntityHandle b;
    float anchorAX = 0;
    float anchorAY = 0;
    float anchorBX = 0;
    float anchorBY = 0;
    float axisX = 0;
    float axisY = 0;
    float linearLowerX = 0;
    float linearLowerY = 0;
    float linearUpperX = 0;
    float linearUpperY = 0;
    float angularLower = 0;
    float angularUpper = 0;
    float motorSpeed = 0;
    float motorEffort = 0;
    float breakForce = 0;
    float breakTorque = 0;
    /// physics::JointAxis each.
    std::uint8_t linearX = 0;
    std::uint8_t linearY = 0;
    std::uint8_t angular = 0;
    /// Nought for none; 1 and 2 drive linear x and y, and 3 the angle.
    std::uint8_t motor = 0;
    /// Whether the two bodies still collide with each other.
    bool collideConnected = false;
    bool broken = false;
};

/// physics3d's Attach3D in two dimensions: an entity with an Attach2D and a
/// Pose2D and no body follows `parent`, whose Pose2D it takes at the offset
/// here, in the parent's frame, after every step. The rotation is a cosine
/// and a sine, all noughts as none. Attachments chain, parents before
/// children; one whose parent has no pose, or that is its own ancestor, or
/// that has a body is left where it is.
struct Attach2D {
    static constexpr schema::ComponentTypeId kComponentTypeId =
        schema::ComponentTypeId::fromText("57e25807-df70-452f-8798-ab3a1137e1f0");
    static constexpr std::string_view kComponentName = "rawframe.physics2d.attach";

    world::EntityHandle parent;
    float x = 0;
    float y = 0;
    float c = 0;
    float s = 0;
};

/// Body2D, Pose2D, Velocity2D, Impulse2D, Contact2D, Character2D, Joint2D,
/// and Attach2D, in that order.
/// An entity field appears as its two parts, `<name>.slot` and
/// `<name>.generation`.
[[nodiscard]] std::span<const schema::ComponentLayout> componentLayouts() noexcept;

/// What an overlap query found: how many bodies overlap the shape asked
/// about, and the one at the asked index among them in entity order (the
/// null entity past the last). Not a component: a value a query answers.
struct Overlap2D {
    std::uint32_t count = 0;
    world::EntityHandle entity;
};

/// RayHit2D and Overlap2D as a script must declare them; their identities
/// are none.
[[nodiscard]] std::span<const schema::ComponentLayout> answerLayouts() noexcept;

} // namespace rawframe::physics2d
