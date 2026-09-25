#pragma once

// Animation in the World (ADR-0039, SPEC-0035): one graph instance for each
// entity with an Animator, played once per tick by one system in the
// simulation phase, `rawframe.animation.step`, in entity order:
//
//   1. instances follow their entities: an entity that gained an Animator
//      gets an instance of its animator's graph, one that lost it or is
//      gone loses it, and one whose Animator names another animator, or
//      another relevance, starts again;
//   2. each instance takes its Animator's transition request, if it has
//      one, and its parameters from the fields of the game's
//      component its animator binds them to, as the last barrier committed
//      them (SPEC-0035's parameter write; a value not of the parameter's
//      type is refused and counted, and the last one kept);
//   3. it advances by the tick (SPEC-0035's parameter and state phase),
//      and its Animator's `events` is written with the events it fired;
//   4. it is posed, and the pose kept in model space for the game's
//      queries until the next step.
//
// A dedicated server plays only animators whose relevance is `Simulation`
// (SPEC-0035's typed server subset); the rest have no instance there.
// Gameplay systems that set parameters run before the step; those that
// read poses or events, after. The same components give the same bits on
// every machine.

#include "rawframe/animation/instance.h"
#include "rawframe/composition/participant.h"
#include "rawframe/result/result.h"
#include "rawframe/schema/layout.h"
#include "rawframe/world/schedule.h"
#include "rawframe/world_animation/components.h"
#include "rawframe/world_runtime/simulation.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string_view>
#include <vector>

namespace rawframe::world_animation {

/// Where one lane of a parameter comes from: a field of the animator's
/// parameter component. A `vec2` has two, lane 0 its x and 1 its y.
struct ParameterField {
    animation::ParameterIndex parameter;
    std::uint8_t lane = 0;
    std::size_t offset = 0;
    schema::FieldType type = schema::FieldType::F32;
};

/// One of the game's animators: a compiled graph, and where its parameters
/// come from.
struct AnimatorSettings {
    std::uint64_t id = 0;
    std::shared_ptr<const animation::CompiledGraph> graph;
    /// The game's component its parameters are read from; none for a graph
    /// that plays on its defaults.
    std::optional<schema::ComponentTypeId> parameters;
    std::vector<ParameterField> fields;
    /// What a dedicated server poses: a byte for each bone of the graph's
    /// skeleton, nonzero for a bone it poses (SPEC-0035), the rest left at
    /// their bind; empty for every bone. A client poses them all.
    std::vector<std::uint8_t> subset;
};

struct AnimationSettings {
    /// Each identity once.
    std::vector<AnimatorSettings> animators;
    /// Plays only animators of `Simulation` relevance: a dedicated server.
    bool simulationOnly = false;
};

struct AnimationStatistics {
    std::uint64_t steps = 0;
    std::uint64_t instancesMade = 0;
    std::uint64_t instancesRemoved = 0;
    /// Animators naming no animator the game has, each counted when it
    /// first does.
    std::uint64_t animatorsRefused = 0;
    /// Parameter values not of their type, each refused.
    std::uint64_t parametersRefused = 0;
    std::uint64_t eventsFired = 0;
    /// Steps in which an instance crossed more events than it reports.
    std::uint64_t eventsOverflowed = 0;
    /// Transition requests made, and those that pushed an older one out.
    std::uint64_t requests = 0;
    std::uint64_t requestsDropped = 0;
};

inline constexpr std::string_view kStepSystem = "rawframe.animation.step";

/// A state machine of an entity's graph as the last step left it: its
/// state, by its place among the machine's states, and how far the
/// transition into it has come, if one is under way (0 to 1).
struct MachineView {
    std::size_t state = 0;
    std::optional<double> progress;
};

/// What the last step left, for the game's queries. Only from the World's
/// thread, between steps or from a system.
class AnimationQueries {
public:
    AnimationQueries() = default;
    AnimationQueries(const AnimationQueries&) = delete;
    AnimationQueries& operator=(const AnimationQueries&) = delete;
    virtual ~AnimationQueries() = default;

    /// The entity's pose in model space (each bone relative to the entity),
    /// in its skeleton's order, or null for an entity not played here.
    [[nodiscard]] virtual const animation::Pose* pose(world::EntityHandle entity) const noexcept = 0;
    /// The events the entity's graph fired in the last step, in order.
    [[nodiscard]] virtual std::span<const animation::GraphEvent> events(world::EntityHandle entity) const noexcept = 0;
    /// The state machine that is the graph node of id `node`; none for an
    /// entity not played here, or a node that is no state machine the
    /// output reaches.
    [[nodiscard]] virtual std::optional<MachineView> machine(world::EntityHandle entity,
                                                             std::uint64_t node) const noexcept = 0;
};

class WorldAnimation final : public world_runtime::SystemContributor, public AnimationQueries {
public:
    /// Refuses (`InvalidSettings`) an animator without a graph or named
    /// twice, and a parameter field of a lane its parameter lacks or bound
    /// twice.
    [[nodiscard]] static result::Result<std::unique_ptr<WorldAnimation>> create(AnimationSettings settings);
    ~WorldAnimation() override;

    /// Contributes kStepSystem. The registry must hold the Animator at the
    /// engine's size and every animator's parameter component, large
    /// enough for its fields.
    [[nodiscard]] result::Status declareSystems(const schema::SchemaRegistry& registry,
                                                std::vector<world::SystemDeclaration>& systems) noexcept override;

    [[nodiscard]] AnimationStatistics statistics() const noexcept;
    /// Every pose and event of the last step, digested in entity order:
    /// equal on two machines exactly when they played the same.
    [[nodiscard]] std::uint64_t digest() const noexcept;

    [[nodiscard]] const animation::Pose* pose(world::EntityHandle entity) const noexcept override;
    [[nodiscard]] std::span<const animation::GraphEvent> events(world::EntityHandle entity) const noexcept override;
    [[nodiscard]] std::optional<MachineView> machine(world::EntityHandle entity,
                                                     std::uint64_t node) const noexcept override;

    struct State;
    explicit WorldAnimation(std::unique_ptr<State> state) noexcept;

private:
    std::unique_ptr<State> state_;
};

/// What the loaded game says about animation; provided by the participant
/// that loads a game.
class AnimationPlan {
public:
    AnimationPlan() = default;
    AnimationPlan(const AnimationPlan&) = delete;
    AnimationPlan& operator=(const AnimationPlan&) = delete;
    virtual ~AnimationPlan() = default;

    /// None for a game without animators.
    [[nodiscard]] virtual const std::optional<AnimationSettings>& animation() const noexcept = 0;
    /// Where the game's scripts ask about poses and events, from the
    /// animation made for it until it goes (null).
    virtual void attach(const AnimationQueries* queries) noexcept = 0;
};

inline constexpr composition::Capability<AnimationPlan> kAnimationPlan{"rawframe.animation.plan"};

} // namespace rawframe::world_animation
