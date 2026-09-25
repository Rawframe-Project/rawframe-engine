#include "rawframe/world_animation/animation.h"

#include "rawframe/base/sha256.h"
#include "rawframe/world/query.h"
#include "rawframe/world_animation/errors.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstring>
#include <map>
#include <set>
#include <tuple>
#include <utility>

namespace rawframe::world_animation {

namespace {

std::unexpected<result::Error> invalid(std::string_view why) {
    return result::fail(
        result::ErrorClass::InvalidArgument, kWorldAnimationDomain, code(WorldAnimationError::InvalidSettings), why);
}

std::size_t sizeOf(schema::FieldType type) noexcept {
    switch (type) {
    case schema::FieldType::U8:
    case schema::FieldType::Bool:
        return 1;
    case schema::FieldType::U32:
    case schema::FieldType::F32:
        return 4;
    case schema::FieldType::U64:
    case schema::FieldType::F64:
        return 8;
    }
    return 8;
}

/// A property's value written into its field, a whole number held within
/// the field's range.
void write(std::byte* data, const PropertyField& field, double value) noexcept {
    std::byte* at = data + field.offset;
    const auto kWhole = [value](double most) {
        return std::clamp(std::trunc(value), 0.0, most);
    };
    switch (field.type) {
    case schema::FieldType::U8:
    case schema::FieldType::Bool: {
        const auto kMade = static_cast<std::uint8_t>(kWhole(255.0));
        std::memcpy(at, &kMade, sizeof kMade);
        return;
    }
    case schema::FieldType::U32: {
        const auto kMade = static_cast<std::uint32_t>(kWhole(4294967295.0));
        std::memcpy(at, &kMade, sizeof kMade);
        return;
    }
    case schema::FieldType::U64: {
        const auto kMade = static_cast<std::uint64_t>(kWhole(4294967295.0));
        std::memcpy(at, &kMade, sizeof kMade);
        return;
    }
    case schema::FieldType::F32: {
        const auto kMade = static_cast<float>(value);
        std::memcpy(at, &kMade, sizeof kMade);
        return;
    }
    case schema::FieldType::F64:
        std::memcpy(at, &value, sizeof value);
        return;
    }
}

/// A field's value as a parameter lane takes it.
double read(const std::byte* data, const ParameterField& field) noexcept {
    const std::byte* at = data + field.offset;
    switch (field.type) {
    case schema::FieldType::U8:
    case schema::FieldType::Bool: {
        std::uint8_t value = 0;
        std::memcpy(&value, at, sizeof value);
        return value;
    }
    case schema::FieldType::U32: {
        std::uint32_t value = 0;
        std::memcpy(&value, at, sizeof value);
        return static_cast<double>(std::bit_cast<std::int32_t>(value));
    }
    case schema::FieldType::U64: {
        std::uint64_t value = 0;
        std::memcpy(&value, at, sizeof value);
        return static_cast<double>(std::bit_cast<std::int64_t>(value));
    }
    case schema::FieldType::F32: {
        float value = 0;
        std::memcpy(&value, at, sizeof value);
        return value;
    }
    case schema::FieldType::F64: {
        double value = 0;
        std::memcpy(&value, at, sizeof value);
        return value;
    }
    }
    return 0.0;
}

} // namespace

struct WorldAnimation::State {
    AnimationSettings settings;
    schema::ComponentRuntimeId animatorComponent;
    /// Each animator's parameter component, beside `settings.animators`.
    std::vector<std::optional<schema::ComponentRuntimeId>> parameters;
    std::vector<schema::ComponentRuntimeId> reads;
    std::vector<schema::ComponentRuntimeId> writes;
    std::optional<world::Query<world::Write<Animator>>> query;
    /// None in a World without root motion.
    std::optional<schema::ComponentKey<RootMotion>> rootMotion;
    /// Each animator's property components, beside its properties.
    std::vector<std::vector<schema::ComponentRuntimeId>> properties;
    std::vector<std::optional<double>> values;
    std::unique_ptr<world::System> system;

    struct Playing {
        std::uint64_t animator = 0;
        std::uint8_t relevance = 0;
        std::size_t settings = 0;
        animation::GraphInstance instance;
        animation::Pose pose;
        std::vector<animation::GraphEvent> events;
    };
    std::map<world::EntityHandle, Playing> playing;
    std::set<world::EntityHandle> refused;
    const world::World* world = nullptr;
    animation::PoseEvaluator evaluator;
    animation::Pose local;
    std::vector<std::pair<world::EntityHandle, Animator*>> rows;
    AnimationStatistics statistics;
    std::uint64_t digest = 0;

    result::Status step(world::World& stepped, world::TickRate rate) {
        // Another World (a checkpoint restored): its entities are not ours.
        if (&stepped != world) {
            playing.clear();
            refused.clear();
            world = &stepped;
        }
        ++statistics.steps;
        rows.clear();
        query->forEach(stepped, [this](world::EntityHandle entity, Animator& animator) {
            rows.emplace_back(entity, &animator);
        });
        std::ranges::sort(rows, {}, [](const auto& row) {
            return row.first;
        });
        const auto kHas = [this](world::EntityHandle entity) {
            return std::ranges::binary_search(rows, entity, {}, [](const auto& row) {
                return row.first;
            });
        };
        statistics.instancesRemoved += std::erase_if(playing, [&kHas](const auto& each) {
            return !kHas(each.first);
        });
        std::erase_if(refused, [&kHas](world::EntityHandle entity) {
            return !kHas(entity);
        });
        const double kDelta = static_cast<double>(rate.seconds) / static_cast<double>(rate.ticks);
        base::Sha256 digested;
        for (const auto& [kEntity, animator] : rows) {
            animator->events = 0;
            const std::uint64_t kRequest = std::exchange(animator->request, 0);
            RootMotion* moved = rootMotion.has_value() ? stepped.get(kEntity, *rootMotion) : nullptr;
            if (moved != nullptr) {
                moved->moveX = moved->moveY = moved->moveZ = 0.0;
                moved->turnX = moved->turnY = moved->turnZ = 0.0;
                moved->turnW = 1.0;
            }
            Playing* played = play(kEntity, *animator);
            if (played == nullptr) {
                continue;
            }
            if (kRequest != 0) {
                ++statistics.requests;
                statistics.requestsDropped += played->instance.request(kRequest) ? 0 : 1;
            }
            takeParameters(stepped, kEntity, *played);
            played->events.clear();
            if (!played->instance.advance(kDelta, played->events)) {
                ++statistics.eventsOverflowed;
            }
            animator->events = static_cast<std::uint32_t>(played->events.size());
            statistics.eventsFired += played->events.size();
            evaluator.evaluate(played->instance,
                               local,
                               settings.simulationOnly
                                   ? std::span<const std::uint8_t>{settings.animators[played->settings].subset}
                                   : std::span<const std::uint8_t>{});
            digested.update(std::as_bytes(std::span{&kEntity, 1}));
            if (moved != nullptr) {
                animation::removeRootMotion(played->instance.graph(), local);
                commitMotion(played->instance.rootMotion(), *moved);
                digested.update(std::as_bytes(std::span{moved, 1}));
            }
            // The graph's stages, on the pose where the entity is; a server
            // runs only the simulation's.
            evaluator.modify(played->instance, local, settings.simulationOnly);
            writeProperties(stepped, kEntity, *played);
            animation::toModelSpace(played->instance.graph().parents(), local, played->pose);
            for (const animation::Transform& bone : played->pose.bones) {
                digested.update(std::as_bytes(std::span{bone.translation}));
                digested.update(std::as_bytes(std::span{bone.rotation}));
                digested.update(std::as_bytes(std::span{bone.scale}));
            }
            for (const animation::GraphEvent& event : played->events) {
                digested.update(std::as_bytes(std::span{&event.event, 1}));
            }
        }
        const base::Sha256Digest kDigest = digested.finish();
        digest = 0;
        for (std::size_t at = 0; at < 8; ++at) {
            digest = (digest << 8U) | std::to_integer<std::uint64_t>(kDigest[at]);
        }
        return {};
    }

    /// The fields the instance's clips animate, onto the entity's
    /// components that have them.
    void writeProperties(world::World& stepped, world::EntityHandle entity, const Playing& played) {
        const std::vector<schema::ComponentRuntimeId>& components = properties[played.settings];
        if (components.empty()) {
            return;
        }
        evaluator.evaluateProperties(played.instance, values);
        const std::vector<PropertyField>& fields = settings.animators[played.settings].properties;
        for (std::size_t at = 0; at < values.size(); ++at) {
            auto* data = static_cast<std::byte*>(stepped.getErased(entity, components[at]));
            if (values[at].has_value() && data != nullptr) {
                write(data, fields[at], *values[at]);
            }
        }
    }

    /// A step's move onto its entity's RootMotion, and onto the whole.
    static void commitMotion(const animation::Transform& move, RootMotion& moved) {
        moved.moveX = move.translation[0];
        moved.moveY = move.translation[1];
        moved.moveZ = move.translation[2];
        moved.turnX = move.rotation[0];
        moved.turnY = move.rotation[1];
        moved.turnZ = move.rotation[2];
        moved.turnW = move.rotation[3];
        animation::Transform whole{.translation = {moved.travelX, moved.travelY, moved.travelZ},
                                   .rotation = {moved.facingX, moved.facingY, moved.facingZ, moved.facingW}};
        const bool kFacing = std::ranges::all_of(whole.rotation,
                                                 [](double each) {
                                                     return std::isfinite(each);
                                                 }) &&
                             whole.rotation != std::array<double, 4>{};
        whole.rotation = kFacing ? whole.rotation : std::array<double, 4>{0.0, 0.0, 0.0, 1.0};
        whole = animation::composed(whole, move);
        moved.travelX = whole.translation[0];
        moved.travelY = whole.translation[1];
        moved.travelZ = whole.translation[2];
        moved.facingX = whole.rotation[0];
        moved.facingY = whole.rotation[1];
        moved.facingZ = whole.rotation[2];
        moved.facingW = whole.rotation[3];
    }

    /// The entity's instance as its Animator now says, made or made again
    /// as needed; null when it plays nothing here.
    Playing* play(world::EntityHandle entity, const Animator& animator) {
        const auto kSettings = std::ranges::lower_bound(settings.animators, animator.graph, {}, &AnimatorSettings::id);
        const bool kKnown = kSettings != settings.animators.end() && kSettings->id == animator.graph &&
                            animator.relevance <= static_cast<std::uint8_t>(animation::Relevance::Simulation);
        auto found = playing.find(entity);
        if (found != playing.end() &&
            (!kKnown || found->second.animator != animator.graph || found->second.relevance != animator.relevance)) {
            playing.erase(found);
            ++statistics.instancesRemoved;
            found = playing.end();
        }
        if (!kKnown) {
            statistics.animatorsRefused += refused.insert(entity).second ? 1U : 0U;
            return nullptr;
        }
        refused.erase(entity);
        if (settings.simulationOnly &&
            animator.relevance != static_cast<std::uint8_t>(animation::Relevance::Simulation)) {
            return nullptr;
        }
        if (found == playing.end()) {
            found = playing
                        .emplace(entity,
                                 Playing{.animator = animator.graph,
                                         .relevance = animator.relevance,
                                         .settings = static_cast<std::size_t>(kSettings - settings.animators.begin()),
                                         .instance = animation::GraphInstance{kSettings->graph},
                                         .pose = {},
                                         .events = {}})
                        .first;
            ++statistics.instancesMade;
        }
        return &found->second;
    }

    /// Parameters from the fields the animator binds them to, as committed.
    void takeParameters(world::World& stepped, world::EntityHandle entity, Playing& played) {
        const std::optional<schema::ComponentRuntimeId>& component = parameters[played.settings];
        if (!component.has_value()) {
            return;
        }
        const auto* data = static_cast<const std::byte*>(stepped.getErased(entity, *component));
        if (data == nullptr) {
            return;
        }
        const std::vector<ParameterField>& fields = settings.animators[played.settings].fields;
        for (std::size_t at = 0; at < fields.size();) {
            const animation::ParameterIndex kParameter = fields[at].parameter;
            animation::ParameterValue value = played.instance.get(kParameter);
            for (; at < fields.size() && fields[at].parameter == kParameter; ++at) {
                value[fields[at].lane] = read(data, fields[at]);
            }
            if (!played.instance.set(kParameter, value).has_value()) {
                ++statistics.parametersRefused;
            }
        }
    }
};

namespace {

class Step final : public world::System {
public:
    explicit Step(WorldAnimation::State& state) noexcept : state_(&state) {
    }
    result::Status run(world::SystemContext& context) noexcept override {
        return state_->step(context.world, context.rate);
    }

private:
    WorldAnimation::State* state_;
};

} // namespace

WorldAnimation::WorldAnimation(std::unique_ptr<State> state) noexcept : state_(std::move(state)) {
}

WorldAnimation::~WorldAnimation() = default;

result::Result<std::unique_ptr<WorldAnimation>> WorldAnimation::create(AnimationSettings settings) {
    std::ranges::sort(settings.animators, {}, &AnimatorSettings::id);
    for (std::size_t at = 0; at < settings.animators.size(); ++at) {
        AnimatorSettings& animator = settings.animators[at];
        if (animator.graph == nullptr || (at > 0 && settings.animators[at - 1].id == animator.id)) {
            return invalid("animation settings: each animator once, with its graph");
        }
        if (!animator.subset.empty() && animator.subset.size() != animator.graph->bindPose().bones.size()) {
            return invalid("animation settings: a subset has a byte for each bone");
        }
        const std::span<const animation::CompiledGraph::Property> kProperties = animator.graph->properties();
        if (animator.properties.size() != kProperties.size()) {
            return invalid("animation settings: a field for each of the graph's properties");
        }
        for (std::size_t property = 0; property < kProperties.size(); ++property) {
            const schema::FieldType kType = animator.properties[property].type;
            const bool kFloat = kType == schema::FieldType::F32 || kType == schema::FieldType::F64;
            const bool kWhole =
                kType == schema::FieldType::U8 || kType == schema::FieldType::U32 || kType == schema::FieldType::U64;
            if (kProperties[property].channel == animation::Channel::Float ? !kFloat : !kWhole) {
                return invalid("animation settings: a float property in an f32 or f64 field, and a discrete one in a "
                               "u8, u32, or u64");
            }
        }
        std::ranges::sort(animator.fields, {}, [](const ParameterField& field) {
            return std::tuple{field.parameter, field.lane};
        });
        for (std::size_t field = 0; field < animator.fields.size(); ++field) {
            const ParameterField& each = animator.fields[field];
            const bool kLane =
                each.parameter.value < animator.graph->parameterCount() &&
                each.lane <
                    (animator.graph->declaration(each.parameter).type == animation::ParameterType::Vec2 ? 2U : 1U);
            const bool kOnce = field == 0 || animator.fields[field - 1].parameter != each.parameter ||
                               animator.fields[field - 1].lane != each.lane;
            if (!kLane || !kOnce || !animator.parameters.has_value()) {
                return invalid("animation settings: a parameter's lanes each bound once, from the animator's "
                               "parameter component");
            }
        }
    }
    auto state = std::make_unique<State>();
    state->settings = std::move(settings);
    return std::make_unique<WorldAnimation>(std::move(state));
}

result::Status WorldAnimation::declareSystems(const schema::SchemaRegistry& registry,
                                              std::vector<world::SystemDeclaration>& systems) noexcept {
    State& state = *state_;
    const auto kAnimator = registry.find(Animator::kComponentTypeId);
    if (!kAnimator.has_value() || registry.descriptor(*kAnimator).size != sizeof(Animator) ||
        !registry.descriptor(*kAnimator).plainData) {
        return invalid("the World does not hold the engine's animation component");
    }
    state.animatorComponent = *kAnimator;
    RAWFRAME_TRY_ASSIGN(state.query, (world::Query<world::Write<Animator>>::resolve(registry)));
    state.parameters.clear();
    state.reads.clear();
    for (const AnimatorSettings& animator : state.settings.animators) {
        if (!animator.parameters.has_value()) {
            state.parameters.emplace_back();
            continue;
        }
        const auto kComponent = registry.find(*animator.parameters);
        if (!kComponent.has_value() || !registry.descriptor(*kComponent).plainData ||
            !std::ranges::all_of(animator.fields, [&](const ParameterField& field) {
                return field.offset + sizeOf(field.type) <= registry.descriptor(*kComponent).size;
            })) {
            return invalid("the World does not hold an animator's parameter component, or its fields");
        }
        state.parameters.emplace_back(*kComponent);
        if (!std::ranges::contains(state.reads, *kComponent)) {
            state.reads.push_back(*kComponent);
        }
    }
    state.writes = {state.animatorComponent};
    state.properties.clear();
    for (const AnimatorSettings& animator : state.settings.animators) {
        std::vector<schema::ComponentRuntimeId>& components = state.properties.emplace_back();
        for (const PropertyField& field : animator.properties) {
            const auto kComponent = registry.find(field.component);
            if (!kComponent.has_value() || !registry.descriptor(*kComponent).plainData ||
                field.offset + sizeOf(field.type) > registry.descriptor(*kComponent).size) {
                return invalid("the World does not hold a component an animator's clips animate, or its field");
            }
            components.push_back(*kComponent);
            if (!std::ranges::contains(state.writes, *kComponent)) {
                state.writes.push_back(*kComponent);
            }
        }
    }
    // What the step writes it may read too.
    std::erase_if(state.reads, [&state](schema::ComponentRuntimeId component) {
        return std::ranges::contains(state.writes, component);
    });
    state.rootMotion.reset();
    if (const auto kMotion = registry.find(RootMotion::kComponentTypeId)) {
        RAWFRAME_TRY_ASSIGN(state.rootMotion, registry.key<RootMotion>());
        state.writes.push_back(*kMotion);
    }
    state.system = std::make_unique<Step>(state);
    systems.push_back(world::SystemDeclaration{.identity = kStepSystem,
                                               .phase = world::Phase::Simulation,
                                               .reads = state.reads,
                                               .writes = state.writes,
                                               .system = state.system.get()});
    return {};
}

AnimationStatistics WorldAnimation::statistics() const noexcept {
    return state_->statistics;
}

std::uint64_t WorldAnimation::digest() const noexcept {
    return state_->digest;
}

const animation::Pose* WorldAnimation::pose(world::EntityHandle entity) const noexcept {
    const auto kFound = state_->playing.find(entity);
    return kFound == state_->playing.end() ? nullptr : &kFound->second.pose;
}

std::span<const animation::GraphEvent> WorldAnimation::events(world::EntityHandle entity) const noexcept {
    const auto kFound = state_->playing.find(entity);
    return kFound == state_->playing.end() ? std::span<const animation::GraphEvent>{} : kFound->second.events;
}

std::optional<MachineView> WorldAnimation::machine(world::EntityHandle entity, std::uint64_t node) const noexcept {
    const auto kFound = state_->playing.find(entity);
    if (kFound == state_->playing.end()) {
        return std::nullopt;
    }
    const animation::GraphInstance& instance = kFound->second.instance;
    const std::optional<std::size_t> kStep = instance.graph().step(node);
    if (!kStep.has_value() || !instance.graph().steps()[*kStep].machine.has_value()) {
        return std::nullopt;
    }
    MachineView view{.state = instance.state(*kStep), .progress = std::nullopt};
    if (const auto kUnderWay = instance.transition(*kStep)) {
        const double kDuration = instance.graph().steps()[*kStep].machine->moves[kUnderWay->first].duration;
        view.progress = kDuration > 0.0 ? std::min(kUnderWay->second / kDuration, 1.0) : 1.0;
    }
    return view;
}

} // namespace rawframe::world_animation
