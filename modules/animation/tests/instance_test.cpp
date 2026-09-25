// Playing graphs: a graph compiled only against its skeleton and the
// clips it is given, weights shared down from the output, rotations
// blended the short way, every playhead moving, and events fired only
// where a node counts.

#include "rawframe/animation/errors.h"
#include "rawframe/animation/instance.h"
#include "rawframe/test/test.h"

#include <cmath>
#include <memory>
#include <vector>

using namespace rawframe;
using namespace rawframe::animation;

namespace {

bool refusedWith(const auto& outcome, AnimationError error) {
    return !outcome.has_value() && outcome.error().domain() == kAnimationDomain &&
           outcome.error().code() == code(error);
}

bool near(double a, double b) {
    return std::abs(a - b) <= 1e-12;
}

constexpr base::Bits128 kSkeletonId{7, 7};
constexpr base::Bits128 kRoot{1, 1};
constexpr base::Bits128 kIdleId{2, 1};
constexpr base::Bits128 kWalkId{2, 2};
constexpr std::uint64_t kMove = 0x6a00000000000001ULL;
constexpr std::uint64_t kSpeed = 0x6a00000000000002ULL;
constexpr std::uint64_t kFootstep = 0x5f3a0c2d9e81b746ULL;
constexpr std::uint64_t kIdleNode = 0x1f00000000000001ULL;
constexpr std::uint64_t kWalkNode = 0x1f00000000000002ULL;
constexpr std::uint64_t kBlendNode = 0x3f00000000000003ULL;

Skeleton rig() {
    return Skeleton{.bones = {Bone{.target = kRoot, .name = "root", .parent = std::nullopt, .bind = {}}}};
}

/// Standing still, facing ahead.
std::shared_ptr<const Clip> idle() {
    return std::make_shared<const Clip>(Clip{
        .skeleton = kSkeletonId,
        .duration = 1.0,
        .loop = Loop::Loop,
        .tracks = {Track{.bone = kRoot, .channel = Channel::Translation, .keys = {Key{.time = 0.0, .value = {}}}}}});
}

/// Two metres a second along x, turned a quarter about z, a step at each
/// half second.
std::shared_ptr<const Clip> walk() {
    const double kHalf = std::sqrt(0.5);
    return std::make_shared<const Clip>(Clip{
        .skeleton = kSkeletonId,
        .duration = 1.0,
        .loop = Loop::Loop,
        .tracks = {Track{
                       .bone = kRoot, .channel = Channel::Translation, .keys = {Key{.time = 0.0, .value = {2, 0, 0}}}},
                   Track{.bone = kRoot,
                         .channel = Channel::Rotation,
                         .keys = {Key{.time = 0.0, .value = {0, 0, kHalf, kHalf}}}}},
        .events = {
            ClipEvent{.event = kFootstep, .name = "footstep", .time = 0.0, .relevance = Relevance::Simulation},
            ClipEvent{.event = kFootstep, .name = "footstep", .time = 0.5, .relevance = Relevance::Simulation}}});
}

Graph locomotion() {
    return Graph{
        .parameters = {Parameter{.name = "armed", .id = 0x6a00000000000004ULL, .type = ParameterType::Bool},
                       Parameter{.name = "move", .id = kMove, .minimum = 0.0, .maximum = 1.0},
                       Parameter{.name = "speed", .id = kSpeed, .initial = {1.0, 0.0}},
                       Parameter{.name = "stance", .id = 0x6a00000000000003ULL, .type = ParameterType::Int}},
        .nodes = {GraphNode{.id = kIdleNode, .node = ClipNode{.clip = kIdleId}},
                  GraphNode{.id = kWalkNode, .node = ClipNode{.clip = kWalkId, .speed = ParameterRef{kSpeed}}},
                  GraphNode{.id = kBlendNode,
                            .node = BlendNode{.inputs = {BlendInput{.name = "idle", .from = {.node = kIdleNode}},
                                                         BlendInput{.name = "walk",
                                                                    .from = {.node = kWalkNode},
                                                                    .weight = ParameterRef{kMove}}}}},
                  GraphNode{.id = 0x4f00000000000004ULL, .node = OutputNode{.pose = {.node = kBlendNode}}}},
        .presentation = {}};
}

std::shared_ptr<const CompiledGraph> compiled(const Graph& graph = locomotion()) {
    const std::vector<NamedClip> kClips{{kIdleId, idle()}, {kWalkId, walk()}};
    auto made = CompiledGraph::compile(graph, rig(), kSkeletonId, kClips);
    RAWFRAME_EXPECT(made.has_value());
    return made.has_value() ? *made : nullptr;
}

} // namespace

RAWFRAME_TEST(AGraphCompilesOnlyAgainstItsClips) {
    const std::vector<NamedClip> kIdleOnly{{kIdleId, idle()}};
    RAWFRAME_EXPECT(refusedWith(CompiledGraph::compile(locomotion(), rig(), kSkeletonId, kIdleOnly),
                                AnimationError::BindingInvalid));
    const std::vector<NamedClip> kClips{{kIdleId, idle()}, {kWalkId, walk()}};
    RAWFRAME_EXPECT(refusedWith(CompiledGraph::compile(locomotion(), rig(), base::Bits128{7, 8}, kClips),
                                AnimationError::BindingInvalid));
    Graph quarantined = locomotion();
    quarantined.nodes[0].node =
        QuarantinedNode{.type = "studio/nodes/pose@1", .record = "{\"type\":\"studio/nodes/pose@1\"}"};
    RAWFRAME_EXPECT(
        refusedWith(CompiledGraph::compile(quarantined, rig(), kSkeletonId, kClips), AnimationError::GraphInvalid));
    // A clamped clip keyed at its duration cannot be looped by its node.
    Clip ending = *idle();
    ending.loop = Loop::Clamp;
    ending.tracks[0].keys.push_back(Key{.time = 1.0});
    const std::vector<NamedClip> kEnding{{kIdleId, std::make_shared<const Clip>(ending)}, {kWalkId, walk()}};
    Graph looped = locomotion();
    std::get<ClipNode>(looped.nodes[0].node).loop = Loop::Loop;
    RAWFRAME_EXPECT(
        refusedWith(CompiledGraph::compile(looped, rig(), kSkeletonId, kEnding), AnimationError::BindingInvalid));
    // What a script and replication name resolve; what they do not, does not.
    const auto kGraph = compiled();
    RAWFRAME_EXPECT(kGraph->parameter("move") == ParameterIndex{1});
    RAWFRAME_EXPECT(kGraph->parameter(kSpeed) == ParameterIndex{2});
    RAWFRAME_EXPECT(!kGraph->parameter("run").has_value() && !kGraph->parameter(std::uint64_t{9}).has_value());
    RAWFRAME_EXPECT(kGraph->steps().size() == 3);
}

RAWFRAME_TEST(ParametersHoldTheirTypeAndBounds) {
    GraphInstance instance{compiled()};
    const ParameterIndex kMoveAt = *instance.graph().parameter("move");
    const ParameterIndex kStance = *instance.graph().parameter("stance");
    const ParameterIndex kArmed = *instance.graph().parameter("armed");
    RAWFRAME_EXPECT(instance.get(*instance.graph().parameter("speed")) == (ParameterValue{1.0, 0.0}));
    RAWFRAME_EXPECT(instance.set(kMoveAt, {3.0, 0.0}).has_value() && instance.get(kMoveAt)[0] == 1.0);
    RAWFRAME_EXPECT(instance.set(kMoveAt, {-3.0, 0.0}).has_value() && instance.get(kMoveAt)[0] == 0.0);
    RAWFRAME_EXPECT(instance.set(kStance, {-4.0, 0.0}).has_value() && instance.get(kStance)[0] == -4.0);
    for (const auto& [kAt, kValue] : {std::pair{kMoveAt, ParameterValue{std::nan(""), 0.0}},
                                      std::pair{kMoveAt, ParameterValue{0.5, 1.0}},
                                      std::pair{kStance, ParameterValue{1.5, 0.0}},
                                      std::pair{kArmed, ParameterValue{2.0, 0.0}},
                                      std::pair{ParameterIndex{9}, ParameterValue{}}}) {
        RAWFRAME_EXPECT(refusedWith(instance.set(kAt, kValue), AnimationError::GraphInvalid));
    }
}

RAWFRAME_TEST(ABlendWeighsItsInputsAndTurnsTheShortWay) {
    GraphInstance instance{compiled()};
    PoseEvaluator evaluator;
    Pose pose;
    std::vector<GraphEvent> events;
    RAWFRAME_EXPECT(instance.set(*instance.graph().parameter("move"), {0.25, 0.0}).has_value());
    RAWFRAME_EXPECT(instance.advance(0.0, events));
    evaluator.evaluate(instance, pose);
    // Weights 1 and 0.25, normalized.
    RAWFRAME_EXPECT(pose.bones.size() == 1 && near(pose.bones[0].translation[0], 0.4));
    RAWFRAME_EXPECT(instance.weight(0) == 0.8 && instance.weight(1) == 0.2 && instance.weight(2) == 1.0);
    // Halfway between facing ahead and a quarter turn is an eighth turn.
    RAWFRAME_EXPECT(instance.set(*instance.graph().parameter("move"), {1.0, 0.0}).has_value());
    RAWFRAME_EXPECT(instance.advance(0.0, events));
    evaluator.evaluate(instance, pose);
    RAWFRAME_EXPECT(near(pose.bones[0].rotation[2], std::sin(std::numbers::pi / 8.0)) &&
                    near(pose.bones[0].rotation[3], std::cos(std::numbers::pi / 8.0)));
    // Nothing weighed: shared evenly.
    Graph unweighed = locomotion();
    std::get<BlendNode>(unweighed.nodes[2].node).inputs[0].weight = 0.0;
    GraphInstance even{compiled(unweighed)};
    RAWFRAME_EXPECT(even.advance(0.0, events));
    RAWFRAME_EXPECT(even.weight(0) == 0.5 && even.weight(1) == 0.5);
    RAWFRAME_EXPECT(events.empty());
}

RAWFRAME_TEST(EveryPlayheadMovesButOnlyWeighedNodesFire) {
    GraphInstance instance{compiled()};
    std::vector<GraphEvent> events;
    // Standing: the walk keeps time without a step heard.
    RAWFRAME_EXPECT(instance.advance(0.75, events));
    RAWFRAME_EXPECT(events.empty() && instance.playhead(1) == 0.75);
    // Walking at double speed: through the wrap and the next half second.
    RAWFRAME_EXPECT(instance.set(*instance.graph().parameter("move"), {1.0, 0.0}).has_value());
    RAWFRAME_EXPECT(instance.set(*instance.graph().parameter("speed"), {2.0, 0.0}).has_value());
    RAWFRAME_EXPECT(instance.advance(0.5, events));
    RAWFRAME_EXPECT(instance.playhead(1) == 0.75 && instance.playhead(0) == 0.25);
    const GraphEvent kStep{.event = kFootstep, .relevance = Relevance::Simulation, .reverse = false, .weight = 0.5};
    RAWFRAME_EXPECT(events == (std::vector<GraphEvent>{kStep, kStep}));
    // Backwards, marked so.
    events.clear();
    RAWFRAME_EXPECT(instance.set(*instance.graph().parameter("speed"), {-1.0, 0.0}).has_value());
    RAWFRAME_EXPECT(instance.advance(0.5, events));
    RAWFRAME_EXPECT(events.size() == 1 && events[0].reverse && instance.playhead(1) == 0.25);
    // Past the limit, the advance says so.
    const std::vector<NamedClip> kClips{{kIdleId, idle()}, {kWalkId, walk()}};
    GraphInstance bounded{*CompiledGraph::compile(locomotion(), rig(), kSkeletonId, kClips, {}, {.maximumEvents = 3})};
    RAWFRAME_EXPECT(bounded.set(*bounded.graph().parameter("move"), {1.0, 0.0}).has_value());
    events.clear();
    RAWFRAME_EXPECT(!bounded.advance(10.0, events) && events.size() == 3);
}

RAWFRAME_TEST(TwoInstancesOfOneGraphAgree) {
    const auto kGraph = compiled();
    GraphInstance first{kGraph};
    GraphInstance second{kGraph};
    PoseEvaluator evaluator;
    std::vector<GraphEvent> firstEvents;
    std::vector<GraphEvent> secondEvents;
    Pose firstPose;
    Pose secondPose;
    for (int tick = 0; tick < 120; ++tick) {
        const ParameterValue kMoving{0.5 + (0.5 * std::sin(tick / 10.0)), 0.0};
        for (GraphInstance* instance : {&first, &second}) {
            RAWFRAME_EXPECT(instance->set(*kGraph->parameter("move"), kMoving).has_value());
        }
        RAWFRAME_EXPECT(first.advance(1.0 / 60.0, firstEvents) && second.advance(1.0 / 60.0, secondEvents));
        evaluator.evaluate(first, firstPose);
        evaluator.evaluate(second, secondPose);
        RAWFRAME_EXPECT(firstPose == secondPose);
    }
    RAWFRAME_EXPECT(firstEvents == secondEvents && firstEvents.size() == 4);
}

RAWFRAME_TEST(AMaskTakesItsBonesFromInsideAndTheRestFromOutside) {
    constexpr base::Bits128 kArm{1, 2};
    constexpr base::Bits128 kWaveId{2, 3};
    constexpr base::Bits128 kStandId{2, 4};
    constexpr base::Bits128 kUpperId{3, 1};
    constexpr std::uint64_t kBreath = 0x5f00000000000001ULL;
    const Skeleton kBody{
        .bones = {Bone{.target = kRoot, .name = "root", .parent = std::nullopt, .bind = {}},
                  Bone{.target = kArm, .name = "arm", .parent = BoneIndex{0}, .bind = {.translation = {1, 0, 0}}}}};
    // Waving: the root five metres along, the arm raised. Standing: the
    // root at rest, breathing at a quarter second.
    const auto kWave = std::make_shared<const Clip>(
        Clip{.skeleton = kSkeletonId,
             .duration = 1.0,
             .loop = Loop::Loop,
             .tracks = {Track{.bone = kRoot, .channel = Channel::Translation, .keys = {Key{.value = {5, 0, 0}}}},
                        Track{.bone = kArm, .channel = Channel::Translation, .keys = {Key{.value = {0, 1, 0}}}}}});
    const auto kStand = std::make_shared<const Clip>(
        Clip{.skeleton = kSkeletonId,
             .duration = 1.0,
             .loop = Loop::Loop,
             .tracks = {Track{.bone = kRoot, .channel = Channel::Translation, .keys = {Key{.value = {}}}}},
             .events = {ClipEvent{.event = kBreath, .name = "breath", .time = 0.25}}});
    const auto kMaskOf = [](double weight) {
        return std::make_shared<const Mask>(Mask{
            .skeleton = kSkeletonId, .chains = {MaskChain{.root = {1, 2}, .descendants = true, .weight = weight}}});
    };
    const Graph kGraph{
        .parameters = {},
        .nodes = {GraphNode{.id = 1, .node = ClipNode{.clip = kWaveId}},
                  GraphNode{.id = 2, .node = ClipNode{.clip = kStandId}},
                  GraphNode{.id = 3, .node = MaskNode{.mask = kUpperId, .inside = {.node = 1}, .outside = {.node = 2}}},
                  GraphNode{.id = 4, .node = OutputNode{.pose = {.node = 3}}}},
        .presentation = {}};
    RAWFRAME_EXPECT(masksOf(kGraph) == std::vector<base::Bits128>{kUpperId});
    const auto kText = writeGraph(kGraph);
    RAWFRAME_EXPECT(kText.has_value() && readGraph(*kText).has_value() && *readGraph(*kText) == kGraph);
    const std::vector<NamedClip> kClips{{kWaveId, kWave}, {kStandId, kStand}};
    const auto kPlay = [&](double weight, std::vector<GraphEvent>& events) {
        const std::vector<NamedMask> kMasks{{kUpperId, kMaskOf(weight)}};
        GraphInstance instance{*CompiledGraph::compile(kGraph, kBody, kSkeletonId, kClips, kMasks)};
        RAWFRAME_EXPECT(instance.advance(0.5, events));
        Pose pose;
        PoseEvaluator{}.evaluate(instance, pose);
        return pose;
    };
    // The arm waves; the root stands, and its breath is heard.
    std::vector<GraphEvent> events;
    const Pose kWhole = kPlay(1.0, events);
    RAWFRAME_EXPECT(kWhole.bones[0].translation[0] == 0.0 && kWhole.bones[1].translation[1] == 1.0 &&
                    kWhole.bones[1].translation[0] == 0.0);
    RAWFRAME_EXPECT(events.size() == 1 && events[0].event == kBreath);
    // At half, the arm halfway between its bind and its wave.
    events.clear();
    const Pose kHalf = kPlay(0.5, events);
    RAWFRAME_EXPECT(near(kHalf.bones[1].translation[0], 0.5) && near(kHalf.bones[1].translation[1], 0.5) &&
                    kHalf.bones[0].translation[0] == 0.0);
    // Without its mask, or with another skeleton's, the graph does not
    // compile.
    RAWFRAME_EXPECT(
        refusedWith(CompiledGraph::compile(kGraph, kBody, kSkeletonId, kClips), AnimationError::BindingInvalid));
    const std::vector<NamedMask> kOther{
        {kUpperId, std::make_shared<const Mask>(Mask{.skeleton = {7, 8}, .chains = {MaskChain{.root = kArm}}})}};
    RAWFRAME_EXPECT(refusedWith(CompiledGraph::compile(kGraph, kBody, kSkeletonId, kClips, kOther),
                                AnimationError::BindingInvalid));
}
