// Animation in the World: instances following their entities' Animators,
// parameters taken from the game's component, poses in model space,
// events counted, and a server playing only what is relevant.

#include "rawframe/test/test.h"
#include "rawframe/world/schedule.h"
#include "rawframe/world/world.h"
#include "rawframe/world_animation/animation.h"
#include "rawframe/world_animation/errors.h"

#include <cmath>
#include <cstddef>
#include <optional>
#include <vector>

using namespace rawframe;
using namespace rawframe::world_animation;

namespace {

/// The game's parameter component: how much the character moves, whether
/// it is armed, and where it aims.
struct Stride {
    static constexpr schema::ComponentTypeId kComponentTypeId =
        schema::ComponentTypeId::fromText("8d6f30c6-1287-473f-83c8-54e46fc912ce");
    static constexpr std::string_view kComponentName = "game.stride";

    float move = 0;
    std::uint8_t armed = 0;
    float aimX = 0;
    float aimY = 0;
};

constexpr base::Bits128 kSkeletonId{7, 7};
constexpr base::Bits128 kRoot{1, 1};
constexpr base::Bits128 kArm{1, 2};
constexpr base::Bits128 kIdleId{2, 1};
constexpr base::Bits128 kWalkId{2, 2};
constexpr std::uint64_t kLocomotion = 0x77;
constexpr std::uint64_t kFootstep = 0x5f3a0c2d9e81b746ULL;

bool near(double a, double b) {
    return std::abs(a - b) <= 1e-12;
}

animation::Skeleton rig() {
    return animation::Skeleton{
        .bones = {
            animation::Bone{.target = kRoot, .name = "root", .parent = std::nullopt, .bind = {}},
            animation::Bone{
                .target = kArm, .name = "arm", .parent = animation::BoneIndex{0}, .bind = {.translation = {1, 0, 0}}}}};
}

/// Idle at the origin; the walk two meters along x turned a quarter about
/// z, a step at each half second.

std::shared_ptr<const animation::CompiledGraph> locomotion() {
    using namespace animation;
    const double kHalf = std::sqrt(0.5);
    const auto kIdle = std::make_shared<const Clip>(
        Clip{.skeleton = kSkeletonId,
             .duration = 1.0,
             .loop = Loop::Loop,
             .tracks = {Track{.bone = kRoot, .channel = Channel::Translation, .keys = {Key{.value = {}}}}}});
    const auto kWalk = std::make_shared<const Clip>(Clip{
        .skeleton = kSkeletonId,
        .duration = 1.0,
        .loop = Loop::Loop,
        .tracks = {Track{.bone = kRoot, .channel = Channel::Translation, .keys = {Key{.value = {2, 0, 0}}}},
                   Track{.bone = kRoot, .channel = Channel::Rotation, .keys = {Key{.value = {0, 0, kHalf, kHalf}}}}},
        .events = {ClipEvent{.event = kFootstep, .name = "footstep", .time = 0.0},
                   ClipEvent{.event = kFootstep, .name = "footstep", .time = 0.5}}});
    const Graph kGraph{
        .parameters = {Parameter{.name = "aim", .id = 1, .type = ParameterType::Vec2},
                       Parameter{.name = "armed", .id = 2, .type = ParameterType::Bool},
                       Parameter{.name = "move", .id = 3, .minimum = 0.0, .maximum = 1.0}},
        .nodes = {GraphNode{.id = 0x11, .node = ClipNode{.clip = kIdleId}},
                  GraphNode{.id = 0x12, .node = ClipNode{.clip = kWalkId}},
                  GraphNode{.id = 0x20,
                            .node = BlendNode{.inputs = {BlendInput{.name = "idle", .from = {.node = 0x11}},
                                                         BlendInput{.name = "walk",
                                                                    .from = {.node = 0x12},
                                                                    .weight = ParameterRef{3}}}}},
                  GraphNode{.id = 0x30, .node = OutputNode{.pose = {.node = 0x20}}}},
        .presentation = {}};
    const std::vector<NamedClip> kClips{{kIdleId, kIdle}, {kWalkId, kWalk}};
    return *CompiledGraph::compile(kGraph, rig(), kSkeletonId, kClips);
}

AnimationSettings settings(bool simulationOnly = false) {
    return AnimationSettings{
        .animators = {AnimatorSettings{
            .id = kLocomotion,
            .graph = locomotion(),
            .parameters = Stride::kComponentTypeId,
            .fields = {ParameterField{.parameter = {2}, .offset = offsetof(Stride, move)},
                       ParameterField{
                           .parameter = {1}, .offset = offsetof(Stride, armed), .type = schema::FieldType::Bool},
                       ParameterField{.parameter = {0}, .offset = offsetof(Stride, aimX)},
                       ParameterField{.parameter = {0}, .lane = 1, .offset = offsetof(Stride, aimY)}}}},
        .simulationOnly = simulationOnly};
}

std::shared_ptr<const schema::SchemaRegistry> registry() {
    schema::RegistryBuilder builder;
    builder.add<Animator>().add<Stride>();
    return *builder.freeze();
}

/// A World with animation, ticked at 60 Hz.
struct Stage {
    std::shared_ptr<const schema::SchemaRegistry> schema = registry();
    world::World world{schema};
    std::unique_ptr<WorldAnimation> animation;
    std::optional<world::Schedule> schedule;
    world::TickIndex tick;

    explicit Stage(AnimationSettings made = settings()) {
        animation = *WorldAnimation::create(std::move(made));
        std::vector<world::SystemDeclaration> declarations;
        RAWFRAME_EXPECT(animation->declareSystems(*schema, declarations).has_value());
        schedule.emplace(*world::Schedule::compile(declarations, *schema));
    }

    world::EntityHandle walker(float move, std::uint8_t relevance = 0) {
        const world::EntityHandle kEntity = *world.create();
        RAWFRAME_EXPECT(
            world.insert(kEntity, *schema->key<Animator>(), Animator{.graph = kLocomotion, .relevance = relevance})
                .has_value());
        RAWFRAME_EXPECT(world.insert(kEntity, *schema->key<Stride>(), Stride{.move = move}).has_value());
        return kEntity;
    }

    void run(int ticks) {
        for (int index = 0; index < ticks; ++index) {
            RAWFRAME_EXPECT(schedule->runTick(world, tick, *world::TickRate::of(60)).has_value());
        }
    }

    Animator& animator(world::EntityHandle entity) {
        return *world.get(entity, *schema->key<Animator>());
    }
    Stride& stride(world::EntityHandle entity) {
        return *world.get(entity, *schema->key<Stride>());
    }
};

} // namespace

RAWFRAME_TEST(APoseFollowsTheGamesParameters) {
    Stage stage;
    const world::EntityHandle kWalker = stage.walker(1.0F);
    RAWFRAME_EXPECT(stage.animation->pose(kWalker) == nullptr);
    stage.run(1);
    const animation::Pose* pose = stage.animation->pose(kWalker);
    RAWFRAME_EXPECT(pose != nullptr && pose->bones.size() == 2);
    if (pose == nullptr) {
        return;
    }
    // Half idle, half walk: a meter along, an eighth turn; the arm a meter
    // out from the root along that turn.
    RAWFRAME_EXPECT(near(pose->bones[0].translation[0], 1.0));
    const double kEighth = std::numbers::pi / 4.0;
    RAWFRAME_EXPECT(near(pose->bones[1].translation[0], 1.0 + std::cos(kEighth)) &&
                    near(pose->bones[1].translation[1], std::sin(kEighth)));
    stage.stride(kWalker).move = 0.0F;
    stage.run(1);
    RAWFRAME_EXPECT(stage.animation->pose(kWalker)->bones[0].translation[0] == 0.0);
}

RAWFRAME_TEST(EventsAreCountedOnTheAnimator) {
    Stage stage;
    const world::EntityHandle kWalker = stage.walker(1.0F);
    std::uint32_t heard = 0;
    for (int tick = 0; tick < 60; ++tick) {
        stage.run(1);
        heard += stage.animator(kWalker).events;
        for (const animation::GraphEvent& event : stage.animation->events(kWalker)) {
            RAWFRAME_EXPECT(event.event == kFootstep && event.weight == 0.5);
        }
    }
    RAWFRAME_EXPECT(heard == 2 && stage.animation->statistics().eventsFired == 2);
    // A tick without a step says so.
    stage.run(1);
    RAWFRAME_EXPECT(stage.animator(kWalker).events == 0 && stage.animation->events(kWalker).empty());
}

RAWFRAME_TEST(InstancesFollowTheirEntities) {
    Stage stage;
    const world::EntityHandle kWalker = stage.walker(1.0F);
    stage.run(1);
    // Another relevance starts it again; an animator the game lacks plays
    // nothing, counted once.
    stage.animator(kWalker).relevance = 1;
    stage.run(1);
    stage.animator(kWalker).graph = 0x78;
    stage.run(3);
    RAWFRAME_EXPECT(stage.animation->pose(kWalker) == nullptr);
    stage.animator(kWalker).graph = kLocomotion;
    stage.run(1);
    RAWFRAME_EXPECT(stage.animation->pose(kWalker) != nullptr);
    RAWFRAME_EXPECT(stage.world.remove(kWalker, *stage.schema->key<Animator>()).has_value());
    stage.run(1);
    RAWFRAME_EXPECT(stage.animation->pose(kWalker) == nullptr);
    const AnimationStatistics kStatistics = stage.animation->statistics();
    RAWFRAME_EXPECT(kStatistics.instancesMade == 3 && kStatistics.instancesRemoved == 3 &&
                    kStatistics.animatorsRefused == 1 && kStatistics.steps == 7);
}

RAWFRAME_TEST(AServerPlaysOnlyWhatIsRelevant) {
    Stage server{settings(true)};
    const world::EntityHandle kShown = server.walker(1.0F, 0);
    const world::EntityHandle kHit = server.walker(1.0F, 1);
    server.run(40);
    RAWFRAME_EXPECT(server.animation->pose(kShown) == nullptr && server.animation->pose(kHit) != nullptr);
    RAWFRAME_EXPECT(server.animator(kShown).events == 0 && server.animation->statistics().eventsFired == 1);
}

RAWFRAME_TEST(ParametersNotOfTheirTypeAreRefused) {
    Stage stage;
    const world::EntityHandle kWalker = stage.walker(1.0F);
    stage.stride(kWalker).armed = 2;
    stage.stride(kWalker).aimY = std::nanf("");
    stage.run(2);
    RAWFRAME_EXPECT(stage.animation->statistics().parametersRefused == 4);
    // The rest still plays.
    RAWFRAME_EXPECT(near(stage.animation->pose(kWalker)->bones[0].translation[0], 1.0));
}

RAWFRAME_TEST(TwoWorldsPlayingAlikeAgree) {
    Stage first;
    Stage second;
    for (Stage* stage : {&first, &second}) {
        stage->walker(0.25F);
        stage->walker(1.0F, 1);
    }
    for (int tick = 0; tick < 90; ++tick) {
        first.run(1);
        second.run(1);
        RAWFRAME_EXPECT(first.animation->digest() == second.animation->digest());
    }
    RAWFRAME_EXPECT(first.animation->digest() != 0);
}

RAWFRAME_TEST(SettingsOutOfTheirRulesAreRefused) {
    const auto kRefused = [](const auto& outcome) {
        return !outcome.has_value() && outcome.error().domain() == kWorldAnimationDomain &&
               outcome.error().code() == code(WorldAnimationError::InvalidSettings);
    };
    AnimationSettings twice = settings();
    twice.animators.push_back(twice.animators.front());
    AnimationSettings noLane = settings();
    noLane.animators[0].fields[0].lane = 1;
    AnimationSettings bound = settings();
    bound.animators[0].fields.push_back(bound.animators[0].fields[0]);
    AnimationSettings unbound = settings();
    unbound.animators[0].parameters.reset();
    AnimationSettings graphless = settings();
    graphless.animators[0].graph = nullptr;
    for (const AnimationSettings& kSettings : {twice, noLane, bound, unbound, graphless}) {
        RAWFRAME_EXPECT(kRefused(WorldAnimation::create(kSettings)));
    }
    // A World without the parameter component, or too small for a field.
    AnimationSettings past = settings();
    past.animators[0].fields[0].offset = sizeof(Stride);
    for (AnimationSettings kSettings : {past}) {
        auto made = WorldAnimation::create(std::move(kSettings));
        std::vector<world::SystemDeclaration> declarations;
        RAWFRAME_EXPECT(made.has_value() && kRefused((*made)->declareSystems(*registry(), declarations)));
    }
    schema::RegistryBuilder bare;
    bare.add<Animator>();
    auto made = WorldAnimation::create(settings());
    std::vector<world::SystemDeclaration> declarations;
    RAWFRAME_EXPECT(made.has_value() && kRefused((*made)->declareSystems(**bare.freeze(), declarations)));
}
