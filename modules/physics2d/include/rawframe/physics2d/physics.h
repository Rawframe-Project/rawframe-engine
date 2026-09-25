#pragma once

// 2D physics in the World (ADR-0028, SPEC-0037) through Maul2D, whose types
// stay behind this module. One physics world per World, stepped once per
// tick by one system in the simulation phase, `rawframe.physics2d.step`:
//
//   1. bodies follow their entities, in entity order: an entity that gained
//      the three body components gets a body, one that lost them or is gone
//      loses it, and a changed Body2D makes the body again;
//   2. what gameplay wrote since the last step goes in: a Pose2D that is
//      not what the last step wrote is a teleport, a Velocity2D likewise a
//      new velocity, and an Impulse2D is applied and cleared;
//   3. the world steps once, with the settings' substeps;
//   4. every Contact2D is written from the step's contact and overlap
//      streams, and every body's pose and velocity are written back.
//
// Gameplay systems that push bodies run before the step (`before
// rawframe.physics2d.step`); those that read where bodies went, after. The
// same components and the same writes give the same bits on every machine:
// nothing here depends on addresses, hash order, or time.

#include "rawframe/composition/participant.h"
#include "rawframe/result/result.h"
#include "rawframe/world/schedule.h"
#include "rawframe/world_runtime/simulation.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <string_view>
#include <vector>

namespace rawframe::physics2d {

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
    /// Contacts and sensor overlaps that began.
    std::uint64_t contactsBegun = 0;
    std::uint64_t overlapsBegun = 0;
};

inline constexpr std::string_view kStepSystem = "rawframe.physics2d.step";

class Physics2D final : public world_runtime::SystemContributor {
public:
    /// Refuses settings out of range (`invalid_settings`), a processor that
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
};

inline constexpr composition::Capability<Physics2DPlan> kPhysics2DPlan{"rawframe.physics2d.plan"};

} // namespace rawframe::physics2d
