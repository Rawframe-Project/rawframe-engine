#pragma once

// 2D physics in the World (ADR-0028, SPEC-0037) through Maul2D, whose types
// stay behind this module. One physics world per World, stepped once per
// tick by one system in the simulation phase, `rawframe.physics2d.step`:
//
//   1. bodies follow their entities, in entity order: an entity that gained
//      the three body components gets a body, one that lost them or is gone
//      loses it, and a changed Body2D makes the body again;
//   2. what gameplay wrote since the last step goes in: a Pose2D that is
//      not what the last step wrote is a teleport, which makes the body
//      again there (so a body put somewhere carries nothing of where it was,
//      and steps as one made there would: what a predicting client relies
//      on), a Velocity2D likewise a new velocity, and an Impulse2D is
//      applied and cleared;
//   3. every character (components.h) finds how far it can move along the
//      velocity gameplay wants and is given the velocity that takes it
//      there; then the world steps once, with the settings' substeps;
//   4. every Contact2D is written from the step's contact and overlap
//      streams, and every body's pose and velocity are written back.
//
// Gameplay systems that push bodies run before the step (`before
// rawframe.physics2d.step`); those that read where bodies went, after. The
// same components and the same writes give the same bits on every machine:
// nothing here depends on addresses, hash order, or time.

#include "rawframe/composition/participant.h"
#include "rawframe/physics2d/components.h"
#include "rawframe/result/result.h"
#include "rawframe/world/schedule.h"
#include "rawframe/world_runtime/simulation.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace rawframe::physics2d {

/// What happens where two collision classes meet (SPEC-0037's closed rule
/// vocabulary): they push each other, overlap and are told, or pass through
/// unaware.
enum class CollisionRule : std::uint8_t {
    Collide,
    Trigger,
    Ignore,
};

/// A collision class: a durable identity (never nought) and its name.
struct CollisionClass {
    std::uint64_t id = 0;
    std::string name;
};

struct CollisionPair {
    std::uint64_t first = 0;
    std::uint64_t second = 0;
    CollisionRule rule = CollisionRule::Collide;
};

/// SPEC-0037's collision document: the classes, the rules between pairs of
/// them (either order), and the rule for every pair not listed, bodies of
/// no class among them. A body whose Body2D names a class not here is not
/// made. A body that is a sensor overlaps what its class does not ignore.
struct CollisionDocument {
    std::vector<CollisionClass> classes;
    std::vector<CollisionPair> rules;
    CollisionRule fallback = CollisionRule::Collide;
};

/// Classes a document may declare.
inline constexpr std::size_t kMaximumCollisionClasses = 30;

struct Physics2DSettings {
    /// Meters a second squared.
    float gravityX = 0;
    float gravityY = -10;
    /// Solver substeps per tick: accuracy, never catch-up.
    std::uint32_t substeps = 4;
    std::uint32_t bodyCapacity = 4096;
    std::uint32_t shapeCapacity = 4096;
    std::uint32_t jointCapacity = 1024;
    bool sleeping = true;
    /// Ticks of every body's pose kept for casting back in time
    /// (SPEC-0041's compensation window); nought keeps none.
    std::uint32_t historyTicks = 64;
    CollisionDocument collision;
};

struct Physics2DStatistics {
    std::uint64_t steps = 0;
    std::uint64_t bodiesMade = 0;
    std::uint64_t bodiesRemoved = 0;
    /// Bodies whose Body2D or Pose2D could not be made (a size that is not
    /// positive, a value that is not finite), until either changes.
    std::uint64_t bodiesRefused = 0;
    std::uint64_t teleports = 0;
    std::uint64_t velocitiesSet = 0;
    std::uint64_t impulses = 0;
    /// Character moves made (components.h's Character2D).
    std::uint64_t characterMoves = 0;
    /// Contacts and sensor overlaps that began.
    std::uint64_t contactsBegun = 0;
    std::uint64_t overlapsBegun = 0;
    /// Rays cast back in time, and those whose moment had to be clamped.
    std::uint64_t raysRewound = 0;
    std::uint64_t rewindsClamped = 0;
};

inline constexpr std::string_view kStepSystem = "rawframe.physics2d.step";

/// Questions about where bodies are, answered from the last step: what
/// gameplay sees between steps (SPEC-0037 queries against committed state).
/// Only from the World's thread, between steps or from a system.
/// A ray's `among` that meets bodies of every class, and of none.
inline constexpr std::uint64_t kEveryClass = 0;

class Physics2DQueries {
public:
    Physics2DQueries() = default;
    Physics2DQueries(const Physics2DQueries&) = delete;
    Physics2DQueries& operator=(const Physics2DQueries&) = delete;
    virtual ~Physics2DQueries() = default;

    /// The closest body along the ray from the origin to the origin plus
    /// `toward`, sensors included, of the collision class whose identity is
    /// `among` (a class the settings do not declare meets nothing), or of any
    /// for kEveryClass.
    [[nodiscard]] virtual RayHit2D
    castRay(double originX, double originY, float towardX, float towardY, std::uint64_t among) const noexcept = 0;
    /// The same ray against every body where it was at an earlier moment
    /// (SPEC-0041 lag compensation): `fraction` 65536ths of the way from
    /// the pose committed at tick `base` to the next, as a client shows it
    /// between states. The moment is clamped into the kept history, and
    /// never past the last step; a body without a trail back to it is tried
    /// where it is and its hit marked discontinuous. Nothing moves.
    [[nodiscard]] virtual RayHit2D castRayAt(double originX,
                                             double originY,
                                             float towardX,
                                             float towardY,
                                             std::uint64_t base,
                                             std::uint16_t fraction,
                                             std::uint64_t among) const noexcept = 0;
};

class Physics2D final : public world_runtime::SystemContributor, public Physics2DQueries {
public:
    /// Refuses settings out of range or a collision document that is not
    /// well formed (`invalid_settings`: a class of identity nought, a name
    /// or identity twice, more than kMaximumCollisionClasses classes, a
    /// rule naming a class not declared, a pair ruled twice), a processor that
    /// cannot run the build's kernels (`unsupported`), and a process with
    /// no room for another physics world (`capacity`).
    [[nodiscard]] static result::Result<std::unique_ptr<Physics2D>> create(const Physics2DSettings& settings);
    ~Physics2D() override;

    /// Contributes kStepSystem. The registry must hold the five physics
    /// components at the engine's sizes.
    [[nodiscard]] result::Status declareSystems(const schema::SchemaRegistry& registry,
                                                std::vector<world::SystemDeclaration>& systems) noexcept override;

    [[nodiscard]] Physics2DStatistics statistics() const noexcept;
    /// The physics world's state digest after the last step: equal on two
    /// machines exactly when every body is.
    [[nodiscard]] std::uint64_t digest() const noexcept;

    [[nodiscard]] RayHit2D
    castRay(double originX, double originY, float towardX, float towardY, std::uint64_t among) const noexcept override;
    [[nodiscard]] RayHit2D castRayAt(double originX,
                                     double originY,
                                     float towardX,
                                     float towardY,
                                     std::uint64_t base,
                                     std::uint16_t fraction,
                                     std::uint64_t among) const noexcept override;

    struct State;
    explicit Physics2D(std::unique_ptr<State> state) noexcept;

private:
    std::unique_ptr<State> state_;
};

/// What the loaded game says about physics; provided by the participant
/// that loads a game.
class Physics2DPlan {
public:
    Physics2DPlan() = default;
    Physics2DPlan(const Physics2DPlan&) = delete;
    Physics2DPlan& operator=(const Physics2DPlan&) = delete;
    virtual ~Physics2DPlan() = default;

    /// None for a game without 2D physics.
    [[nodiscard]] virtual const std::optional<Physics2DSettings>& physics2d() const noexcept = 0;
    /// Where the game's scripts ask about bodies, from the physics made for
    /// it until that physics goes (null).
    virtual void attach(const Physics2DQueries* queries) noexcept = 0;
};

inline constexpr composition::Capability<Physics2DPlan> kPhysics2DPlan{"rawframe.physics2d.plan"};

} // namespace rawframe::physics2d
