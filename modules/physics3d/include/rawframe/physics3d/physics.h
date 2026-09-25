#pragma once

// 3D physics in the World (ADR-0028, SPEC-0037) through Maul3D, whose types
// stay behind this module: physics2d's integration in three dimensions. One
// physics world per World, stepped once per tick by one system in the
// simulation phase, `rawframe.physics3d.step`:
//
//   1. bodies follow their entities, in entity order: an entity that gained
//      the three body components gets a body, one that lost them or is gone
//      loses it, and a changed Body3D makes the body again;
//   2. what gameplay wrote since the last step goes in: a Pose3D that is
//      not what the last step wrote is a teleport, which makes the body
//      again there, a Velocity3D likewise a new velocity, and an Impulse3D
//      is applied and cleared;
//   3. every character (components.h) finds how far it can move along the
//      velocity gameplay wants and is given the velocity that takes it
//      there; then the world steps once, with the settings' substeps;
//   4. every Contact3D is written from the step's contact, hit, and overlap
//      streams, and every body's pose and velocity are written back.
//
// Gameplay systems that push bodies run before the step (`before
// rawframe.physics3d.step`); those that read where bodies went, after. The
// same components and the same writes give the same bits on every machine.

#include "rawframe/composition/participant.h"
#include "rawframe/physics/collision.h"
#include "rawframe/physics/rewind.h"
#include "rawframe/physics3d/components.h"
#include "rawframe/result/result.h"
#include "rawframe/world/schedule.h"
#include "rawframe/world_runtime/simulation.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <string_view>
#include <vector>

namespace rawframe::physics3d {

struct Physics3DSettings {
    /// Meters a second squared.
    float gravityX = 0;
    float gravityY = -10;
    float gravityZ = 0;
    /// Solver substeps per tick: accuracy, never catch-up.
    std::uint32_t substeps = 4;
    std::uint32_t bodyCapacity = 4096;
    std::uint32_t shapeCapacity = 4096;
    std::uint32_t jointCapacity = 1024;
    bool sleeping = true;
    /// Ticks of every body's pose kept for casting back in time
    /// (SPEC-0041's compensation window); nought keeps none.
    std::uint32_t historyTicks = 64;
    physics::CollisionDocument collision;
};

struct Physics3DStatistics {
    std::uint64_t steps = 0;
    std::uint64_t bodiesMade = 0;
    std::uint64_t bodiesRemoved = 0;
    /// Bodies whose Body3D or Pose3D could not be made (a size that is not
    /// positive, a value that is not finite), until either changes.
    std::uint64_t bodiesRefused = 0;
    std::uint64_t teleports = 0;
    std::uint64_t velocitiesSet = 0;
    std::uint64_t impulses = 0;
    /// Character moves made (components.h's Character3D).
    std::uint64_t characterMoves = 0;
    /// Contacts and sensor overlaps that began.
    std::uint64_t contactsBegun = 0;
    std::uint64_t overlapsBegun = 0;
    /// Rays cast back in time, and those whose moment had to be clamped.
    std::uint64_t raysRewound = 0;
    std::uint64_t rewindsClamped = 0;
};

inline constexpr std::string_view kStepSystem = "rawframe.physics3d.step";

/// Questions about where bodies are, answered from the last step (SPEC-0037
/// queries against committed state). Only from the World's thread, between
/// steps or from a system.
class Physics3DQueries {
public:
    Physics3DQueries() = default;
    Physics3DQueries(const Physics3DQueries&) = delete;
    Physics3DQueries& operator=(const Physics3DQueries&) = delete;
    virtual ~Physics3DQueries() = default;

    /// The closest body along the ray from the origin to the origin plus
    /// `toward`, sensors included, of the collision class whose identity is
    /// `among` (a class the settings do not declare meets nothing), or of any
    /// for physics::kEveryClass.
    [[nodiscard]] virtual RayHit3D castRay(double originX,
                                           double originY,
                                           double originZ,
                                           float towardX,
                                           float towardY,
                                           float towardZ,
                                           std::uint64_t among) const noexcept = 0;
    /// The same ray against every body where it was at an earlier moment
    /// (SPEC-0041 lag compensation). The moment is clamped into the kept
    /// history, and never past the last step; a body without a trail back
    /// to it is tried where it is and its hit marked discontinuous, and one
    /// the moment's gate keeps is tried where it is. Nothing moves.
    [[nodiscard]] virtual RayHit3D castRayAt(double originX,
                                             double originY,
                                             double originZ,
                                             float towardX,
                                             float towardY,
                                             float towardZ,
                                             const physics::Moment& moment,
                                             std::uint64_t among) const noexcept = 0;
};

class Physics3D final : public world_runtime::SystemContributor, public Physics3DQueries {
public:
    /// Refuses settings out of range (`invalid_settings`), a collision
    /// document that is not well formed (rawframe.physics's
    /// `invalid_document`), a processor that cannot run the build's kernels
    /// (`unsupported`), and a process with no room for another physics world
    /// (`capacity`; Maul3D keeps 64).
    [[nodiscard]] static result::Result<std::unique_ptr<Physics3D>> create(const Physics3DSettings& settings);
    ~Physics3D() override;

    /// Contributes kStepSystem. The registry must hold the six physics
    /// components at the engine's sizes.
    [[nodiscard]] result::Status declareSystems(const schema::SchemaRegistry& registry,
                                                std::vector<world::SystemDeclaration>& systems) noexcept override;

    [[nodiscard]] Physics3DStatistics statistics() const noexcept;
    /// The physics world's state digest after the last step: equal on two
    /// machines exactly when every body is.
    [[nodiscard]] std::uint64_t digest() const noexcept;

    [[nodiscard]] RayHit3D castRay(double originX,
                                   double originY,
                                   double originZ,
                                   float towardX,
                                   float towardY,
                                   float towardZ,
                                   std::uint64_t among) const noexcept override;
    [[nodiscard]] RayHit3D castRayAt(double originX,
                                     double originY,
                                     double originZ,
                                     float towardX,
                                     float towardY,
                                     float towardZ,
                                     const physics::Moment& moment,
                                     std::uint64_t among) const noexcept override;

    struct State;
    explicit Physics3D(std::unique_ptr<State> state) noexcept;

private:
    std::unique_ptr<State> state_;
};

/// What the loaded game says about 3D physics; provided by the participant
/// that loads a game.
class Physics3DPlan {
public:
    Physics3DPlan() = default;
    Physics3DPlan(const Physics3DPlan&) = delete;
    Physics3DPlan& operator=(const Physics3DPlan&) = delete;
    virtual ~Physics3DPlan() = default;

    /// None for a game without 3D physics.
    [[nodiscard]] virtual const std::optional<Physics3DSettings>& physics3d() const noexcept = 0;
    /// Where the game's scripts ask about bodies, from the physics made for
    /// it until that physics goes (null).
    virtual void attach(const Physics3DQueries* queries) noexcept = 0;
};

inline constexpr composition::Capability<Physics3DPlan> kPhysics3DPlan{"rawframe.physics3d.plan"};

} // namespace rawframe::physics3d
