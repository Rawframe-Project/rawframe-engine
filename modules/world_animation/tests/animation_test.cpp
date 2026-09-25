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
    builder.add<Animator>().add<Stride>().add<RootMotion>();
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

RAWFRAME_TEST(AServerPosesOnlyItsSubset) {
    // A reach: the root a meter along x, the arm three out from it rather
    // than its bind's one.
    using namespace animation;
    const auto kReach = std::make_shared<const Clip>(
        Clip{.skeleton = kSkeletonId,
             .duration = 1.0,
             .loop = Loop::Loop,
             .tracks = {Track{.bone = kRoot, .channel = Channel::Translation, .keys = {Key{.value = {1, 0, 0}}}},
                        Track{.bone = kArm, .channel = Channel::Translation, .keys = {Key{.value = {3, 0, 0}}}}}});
    const Graph kGraph{.parameters = {},
                       .nodes = {GraphNode{.id = 1, .node = ClipNode{.clip = kWalkId}},
                                 GraphNode{.id = 2, .node = OutputNode{.pose = {.node = 1}}}},
                       .presentation = {}};
    const std::vector<NamedClip> kClips{{kWalkId, kReach}};
    const auto kReaching = *CompiledGraph::compile(kGraph, rig(), kSkeletonId, kClips);
    // Where the arm is, played for a tick on a server or a client, with the
    // root alone as the subset.
    const auto kArmAt = [&kReaching](bool simulationOnly) {
        Stage stage{
            AnimationSettings{.animators = {AnimatorSettings{.id = kLocomotion, .graph = kReaching, .subset = {1, 0}}},
                              .simulationOnly = simulationOnly}};
        const world::EntityHandle kEntity = stage.walker(0.0F, 1);
        stage.run(1);
        const animation::Pose* pose = stage.animation->pose(kEntity);
        RAWFRAME_EXPECT(pose != nullptr && pose->bones.size() == 2);
        return pose != nullptr ? pose->bones[1].translation[0] : 0.0;
    };
    // The server leaves the arm at its bind; a client poses it whatever
    // the subset says.
    RAWFRAME_EXPECT(kArmAt(true) == 2.0 && kArmAt(false) == 4.0);
}

RAWFRAME_TEST(RootMotionMovesTheCharacterNotItsPose) {
    // Two meters a second along x, taken by the skeleton's source.
    using namespace animation;
    Skeleton walker = rig();
    walker.rootMotion = RootMotionSource{.translation = {true, false, false}};
    const auto kWalk =
        std::make_shared<const Clip>(Clip{.skeleton = kSkeletonId,
                                          .duration = 1.0,
                                          .loop = Loop::Loop,
                                          .tracks = {Track{.bone = kRoot,
                                                           .channel = Channel::Translation,
                                                           .keys = {Key{.value = {}}},
                                                           .drift = std::array<double, 4>{2, 0, 0, 0}}}});
    const Graph kGraph{.parameters = {},
                       .nodes = {GraphNode{.id = 1, .node = ClipNode{.clip = kWalkId}},
                                 GraphNode{.id = 2, .node = OutputNode{.pose = {.node = 1}}}},
                       .presentation = {}};
    const std::vector<NamedClip> kClips{{kWalkId, kWalk}};
    Stage stage{AnimationSettings{
        .animators = {AnimatorSettings{.id = kLocomotion,
                                       .graph = *CompiledGraph::compile(kGraph, walker, kSkeletonId, kClips)}}}};
    const world::EntityHandle kMoved = stage.walker(0.0F);
    const world::EntityHandle kKept = stage.walker(0.0F);
    RAWFRAME_EXPECT(stage.world.insert(kMoved, *stage.schema->key<RootMotion>(), RootMotion{}).has_value());
    stage.run(30);
    // Half a second: a meter traveled, a thirtieth of it in the last step,
    // and the pose left where the entity is. Without the component the
    // motion stays in the pose.
    const RootMotion& motion = *stage.world.get(kMoved, *stage.schema->key<RootMotion>());
    RAWFRAME_EXPECT(std::abs(motion.travelX - 1.0) < 1e-9 && std::abs(motion.moveX - (1.0 / 30.0)) < 1e-9);
    RAWFRAME_EXPECT(motion.turnW == 1.0 && motion.facingW == 1.0 && motion.travelY == 0.0);
    RAWFRAME_EXPECT(stage.animation->pose(kMoved)->bones[0].translation[0] == 0.0);
    RAWFRAME_EXPECT(std::abs(stage.animation->pose(kKept)->bones[0].translation[0] - 1.0) < 1e-9);
    // Playing nothing, it moves nowhere, and the whole stays.
    stage.animator(kMoved).graph = 0x78;
    stage.run(1);
    const RootMotion& still = *stage.world.get(kMoved, *stage.schema->key<RootMotion>());
    RAWFRAME_EXPECT(still.moveX == 0.0 && std::abs(still.travelX - 1.0) < 1e-9);
}

RAWFRAME_TEST(StagesTurnThePoseWhereItIsDrawn) {
    // Standing still, the root turned so its x points along y: the arm a
    // meter along y. A presentation stage, so a server leaves it be.
    using namespace animation;
    const auto kStill = std::make_shared<const Clip>(
        Clip{.skeleton = kSkeletonId,
             .duration = 1.0,
             .loop = Loop::Loop,
             .tracks = {Track{.bone = kRoot, .channel = Channel::Translation, .keys = {Key{.value = {}}}}}});
    const Graph kGraph{
        .parameters = {},
        .nodes = {GraphNode{.id = 1, .node = ClipNode{.clip = kIdleId}},
                  GraphNode{.id = 2, .node = OutputNode{.pose = {.node = 1}}}},
        .modifiers = {Modifier{.stage = LookAt{.bone = kRoot, .goal = {0.0, 5.0, 0.0}, .axis = {1, 0, 0}}}},
        .presentation = {}};
    const std::vector<NamedClip> kClips{{kIdleId, kStill}};
    const auto kArmAt = [&](bool simulationOnly) {
        Stage stage{AnimationSettings{
            .animators = {AnimatorSettings{.id = kLocomotion,
                                           .graph = *CompiledGraph::compile(kGraph, rig(), kSkeletonId, kClips)}},
            .simulationOnly = simulationOnly}};
        const world::EntityHandle kEntity = stage.walker(0.0F, 1);
        stage.run(1);
        return stage.animation->pose(kEntity)->bones[1].translation;
    };
    const std::array<double, 3> kShown = kArmAt(false);
    RAWFRAME_EXPECT(std::abs(kShown[0]) < 1e-12 && std::abs(kShown[1] - 1.0) < 1e-12);
    RAWFRAME_EXPECT(kArmAt(true) == (std::array<double, 3>{1, 0, 0}));
}

RAWFRAME_TEST(ClipsAnimateTheGamesFields) {
    // A clip easing the stride's aim from nought to two over a second and
    // stepping it armed at the half: written onto the entity each step.
    using namespace animation;
    const PropertyBinding kAim{.component = Stride::kComponentTypeId.value, .field = "aim_x"};
    const PropertyBinding kArmed{.component = Stride::kComponentTypeId.value, .field = "armed"};
    const auto kReady = std::make_shared<const Clip>(
        Clip{.duration = 1.0,
             .loop = Loop::Clamp,
             .tracks = {Track{.channel = Channel::Float,
                              .keys = {Key{.value = {}}, Key{.time = 1.0, .value = {2, 0, 0, 0}}},
                              .property = kAim},
                        Track{.channel = Channel::Discrete,
                              .keys = {Key{.value = {}, .interpolation = Interpolation::Step},
                                       Key{.time = 0.5, .value = {1, 0, 0, 0}, .interpolation = Interpolation::Step}},
                              .property = kArmed}}});
    const Graph kGraph{.parameters = {},
                       .nodes = {GraphNode{.id = 1, .node = ClipNode{.clip = kWalkId}},
                                 GraphNode{.id = 2, .node = OutputNode{.pose = {.node = 1}}}},
                       .presentation = {}};
    const std::vector<NamedClip> kClips{{kWalkId, kReady}};
    AnimatorSettings animator{.id = kLocomotion,
                              .graph = *CompiledGraph::compile(kGraph, rig(), kSkeletonId, kClips),
                              .properties = {PropertyField{.component = Stride::kComponentTypeId,
                                                           .offset = offsetof(Stride, aimX),
                                                           .type = schema::FieldType::F32},
                                             PropertyField{.component = Stride::kComponentTypeId,
                                                           .offset = offsetof(Stride, armed),
                                                           .type = schema::FieldType::U8}}};
    Stage stage{AnimationSettings{.animators = {animator}}};
    const world::EntityHandle kEntity = stage.walker(0.0F);
    stage.run(15);
    RAWFRAME_EXPECT(std::abs(stage.stride(kEntity).aimX - 0.5F) < 1e-6F && stage.stride(kEntity).armed == 0);
    stage.run(30);
    RAWFRAME_EXPECT(std::abs(stage.stride(kEntity).aimX - 1.5F) < 1e-6F && stage.stride(kEntity).armed == 1);
    // A field of the wrong kind, or one short, is refused.
    AnimatorSettings wrong = animator;
    wrong.properties[1].type = schema::FieldType::F32;
    AnimatorSettings shortOne = animator;
    shortOne.properties.pop_back();
    for (const AnimatorSettings& kSettings : {wrong, shortOne}) {
        RAWFRAME_EXPECT(!WorldAnimation::create(AnimationSettings{.animators = {kSettings}}).has_value());
    }
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

RAWFRAME_TEST(AnAnimatorsRequestTakesATransitionForOneStep) {
    using namespace animation;
    constexpr std::uint64_t kDoor = 0x99;
    constexpr std::uint64_t kMachine = 0x40;
    constexpr std::uint64_t kOpen = 0x7e00000000000001ULL;
    const auto kShut = std::make_shared<const Clip>(
        Clip{.skeleton = kSkeletonId,
             .duration = 1.0,
             .loop = Loop::Loop,
             .tracks = {Track{.bone = kRoot, .channel = Channel::Translation, .keys = {Key{.value = {}}}}}});
    const auto kAjar = std::make_shared<const Clip>(
        Clip{.skeleton = kSkeletonId,
             .duration = 1.0,
             .loop = Loop::Loop,
             .tracks = {Track{.bone = kRoot, .channel = Channel::Translation, .keys = {Key{.value = {1, 0, 0}}}}}});
    // A door that opens over half a second when asked. Its states are in
    // name order: open, then shut.
    const Graph kGraph{
        .parameters = {},
        .nodes = {GraphNode{.id = 0x11, .node = ClipNode{.clip = kIdleId}},
                  GraphNode{.id = 0x12, .node = ClipNode{.clip = kWalkId}},
                  GraphNode{.id = kMachine,
                            .node =
                                StateMachineNode{.states = {State{.name = "open", .from = {.node = 0x12}},
                                                            State{.name = "shut", .from = {.node = 0x11}}},
                                                 .entry = "shut",
                                                 .transitions = {Transition{.from = "shut",
                                                                            .to = "open",
                                                                            .duration = 0.5,
                                                                            .conditions = {EventCondition{kOpen}}}}}},
                  GraphNode{.id = 0x50, .node = OutputNode{.pose = {.node = kMachine}}}},
        .presentation = {}};
    const std::vector<NamedClip> kClips{{kIdleId, kShut}, {kWalkId, kAjar}};
    Stage stage{AnimationSettings{
        .animators = {AnimatorSettings{.id = kDoor,
                                       .graph = *CompiledGraph::compile(kGraph, rig(), kSkeletonId, kClips),
                                       .parameters = std::nullopt,
                                       .fields = {}}}}};
    const world::EntityHandle kEntity = *stage.world.create();
    RAWFRAME_EXPECT(stage.world.insert(kEntity, *stage.schema->key<Animator>(), Animator{.graph = kDoor}).has_value());
    stage.run(2);
    const auto kShutNow = stage.animation->machine(kEntity, kMachine);
    RAWFRAME_EXPECT(kShutNow.has_value() && kShutNow->state == 1 && !kShutNow->progress.has_value());
    // Asked once: the step takes it and clears the request, and the door
    // starts to open; a step later it is a thirtieth of the way.
    stage.animator(kEntity).request = kOpen;
    stage.run(1);
    const auto kTaken = stage.animation->machine(kEntity, kMachine);
    RAWFRAME_EXPECT(stage.animator(kEntity).request == 0 && kTaken.has_value() && kTaken->state == 0 &&
                    kTaken->progress == 0.0);
    stage.run(1);
    const auto kOpening = stage.animation->machine(kEntity, kMachine);
    RAWFRAME_EXPECT(kOpening.has_value() && kOpening->progress.has_value() && near(*kOpening->progress, 1.0 / 30.0));
    stage.run(30);
    const auto kOpened = stage.animation->machine(kEntity, kMachine);
    RAWFRAME_EXPECT(kOpened.has_value() && kOpened->state == 0 && !kOpened->progress.has_value());
    // A clip node is no machine, and an entity not played has none.
    RAWFRAME_EXPECT(!stage.animation->machine(kEntity, 0x11).has_value() &&
                    !stage.animation->machine(*stage.world.create(), kMachine).has_value());
    RAWFRAME_EXPECT(stage.animation->statistics().requests == 1 && stage.animation->statistics().requestsDropped == 0);
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
    AnimationSettings uneven = settings(true);
    uneven.animators[0].subset = {1};
    for (const AnimationSettings& kSettings : {twice, noLane, bound, unbound, graphless, uneven}) {
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
