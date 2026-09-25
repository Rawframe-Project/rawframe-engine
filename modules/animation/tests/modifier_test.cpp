// Modifier stages (SPEC-0035, D138): two-bone IK and look-at declared on a
// graph, run in order on its pose, by weight and relevance.

#include "rawframe/animation/errors.h"
#include "rawframe/animation/instance.h"
#include "rawframe/test/test.h"

#include <cmath>
#include <memory>
#include <string>
#include <vector>

using namespace rawframe;
using namespace rawframe::animation;

namespace {

bool refusedWith(const auto& outcome, AnimationError error) {
    return !outcome.has_value() && outcome.error().domain() == kAnimationDomain &&
           outcome.error().code() == code(error);
}

bool near(double a, double b) {
    return std::abs(a - b) <= 1e-9;
}

double distance(const std::array<double, 3>& a, const std::array<double, 3>& b) {
    return std::sqrt(((a[0] - b[0]) * (a[0] - b[0])) + ((a[1] - b[1]) * (a[1] - b[1])) +
                     ((a[2] - b[2]) * (a[2] - b[2])));
}

constexpr base::Bits128 kSkeletonId{7, 7};
constexpr base::Bits128 kHips{1, 1};
constexpr base::Bits128 kShoulder{1, 2};
constexpr base::Bits128 kElbow{1, 3};
constexpr base::Bits128 kWrist{1, 4};
constexpr base::Bits128 kHoldId{2, 1};
constexpr std::uint64_t kReach = 0x6a00000000000001ULL;
constexpr std::uint64_t kGoalY = 0x6a00000000000002ULL;

/// Hips a meter up, and an arm straight out along x from them, a meter a
/// bone.
Skeleton rig() {
    return Skeleton{
        .bones = {Bone{.target = kHips, .name = "hips", .parent = std::nullopt, .bind = {.translation = {0, 0, 1}}},
                  Bone{.target = kShoulder, .name = "shoulder", .parent = BoneIndex{0}, .bind = {}},
                  Bone{.target = kElbow, .name = "elbow", .parent = BoneIndex{1}, .bind = {.translation = {1, 0, 0}}},
                  Bone{.target = kWrist, .name = "wrist", .parent = BoneIndex{2}, .bind = {.translation = {1, 0, 0}}}}};
}

/// Holding the bind, with `modifiers`, reach and goal_y parameters.
Graph holding(std::vector<Modifier> modifiers) {
    return Graph{.parameters = {Parameter{.name = "goal_y", .id = kGoalY, .initial = {1.0, 0.0}},
                                Parameter{.name = "reach", .id = kReach, .initial = {1.0, 0.0}}},
                 .nodes = {GraphNode{.id = 1, .node = ClipNode{.clip = kHoldId}},
                           GraphNode{.id = 2, .node = OutputNode{.pose = {.node = 1}}}},
                 .modifiers = std::move(modifiers),
                 .presentation = {}};
}

result::Result<std::shared_ptr<const CompiledGraph>> compiled(const Graph& graph) {
    const std::vector<NamedClip> kClips{
        {kHoldId,
         std::make_shared<const Clip>(Clip{
             .skeleton = kSkeletonId,
             .duration = 1.0,
             .loop = Loop::Loop,
             .tracks = {Track{.bone = kHips, .channel = Channel::Translation, .keys = {Key{.value = {0, 0, 1}}}}}})}};
    return CompiledGraph::compile(graph, rig(), kSkeletonId, kClips);
}

/// The model pose after the stages, all of them or the simulation's.
Pose modified(const Graph& graph, bool simulationOnly = false, double reach = 1.0) {
    const auto kCompiled = compiled(graph);
    RAWFRAME_EXPECT(kCompiled.has_value());
    if (!kCompiled.has_value()) {
        return {};
    }
    GraphInstance instance{*kCompiled};
    RAWFRAME_EXPECT(instance.set(*instance.graph().parameter("reach"), {reach, 0.0}).has_value());
    std::vector<GraphEvent> events;
    RAWFRAME_EXPECT(instance.advance(0.0, events));
    PoseEvaluator evaluator;
    Pose local;
    evaluator.evaluate(instance, local);
    evaluator.modify(instance, local, simulationOnly);
    Pose model;
    toModelSpace(instance.graph().parents(), local, model);
    return model;
}

/// Reaching a goal a meter along x and `goal_y` along y from the shoulder,
/// bending up, by `reach`.
Modifier reaching(std::optional<Triple> pole = Triple{0.0, 0.0, 5.0}) {
    return Modifier{.stage = TwoBoneIk{.tip = kWrist, .goal = {1.0, ParameterRef{kGoalY}, 1.0}, .pole = pole},
                    .weight = ParameterRef{kReach},
                    .relevance = Relevance::Simulation};
}

} // namespace

RAWFRAME_TEST(ModifiersHaveOneText) {
    const Graph kGraph =
        holding({reaching(), Modifier{.stage = LookAt{.bone = kHips, .goal = {0.0, 1.0, 0.0}, .axis = {1, 0, 0}}}});
    const auto kText = writeGraph(kGraph);
    RAWFRAME_EXPECT(kText.has_value() && readGraph(*kText) == kGraph);
    RAWFRAME_EXPECT(kText.has_value() && kText->contains("\"modifiers\": [\n    {\n      \"type\": "
                                                         "\"rawframe/two_bone_ik@1\",\n      \"params\": {\n"));
    // The stages count in the meaning.
    const auto kWith = semanticHash(kGraph);
    const auto kWithout = semanticHash(holding({}));
    RAWFRAME_EXPECT(kWith.has_value() && kWithout.has_value() && *kWith != *kWithout);
    if (!kText.has_value()) {
        return;
    }
    // A type it does not know, a param it does not have, and a look-at
    // along no axis.
    for (const auto& [kFrom, kTo] : std::vector<std::pair<std::string, std::string>>{
             {"rawframe/look_at@1", "rawframe/aim@1"}, {"\"weight\"", "\"heft\""}}) {
        std::string wrong = *kText;
        wrong.replace(wrong.find(kFrom), kFrom.size(), kTo);
        RAWFRAME_EXPECT(refusedWith(readGraph(wrong), AnimationError::GraphInvalid));
    }
    RAWFRAME_EXPECT(refusedWith(writeGraph(holding({Modifier{.stage = LookAt{.bone = kHips, .axis = {0, 0, 0}}}})),
                                AnimationError::GraphInvalid));
    RAWFRAME_EXPECT(refusedWith(writeGraph(holding(std::vector<Modifier>(17, reaching()))), AnimationError::OverLimit));
    // A tip the skeleton lacks, or one without a grandparent.
    for (const base::Bits128 kTip : {base::Bits128{9, 9}, kShoulder}) {
        Modifier wrong = reaching();
        std::get<TwoBoneIk>(wrong.stage).tip = kTip;
        RAWFRAME_EXPECT(refusedWith(compiled(holding({wrong})), AnimationError::BindingInvalid));
    }
}

RAWFRAME_TEST(TwoBoneIkReachesItsGoal) {
    const Pose kPose = modified(holding({reaching()}));
    const std::array<double, 3> kGoal{1.0, 1.0, 1.0};
    // The wrist on the goal, each bone its own length, the elbow bent up
    // toward the pole.
    RAWFRAME_EXPECT(distance(kPose.bones[3].translation, kGoal) < 1e-9);
    RAWFRAME_EXPECT(near(distance(kPose.bones[1].translation, kPose.bones[2].translation), 1.0) &&
                    near(distance(kPose.bones[2].translation, kPose.bones[3].translation), 1.0));
    RAWFRAME_EXPECT(kPose.bones[2].translation[2] > 1.5);
    // Without a pole it keeps the plane it bends in; straight, some square
    // plane, but still the goal.
    const Pose kPoleless = modified(holding({reaching(std::nullopt)}));
    RAWFRAME_EXPECT(distance(kPoleless.bones[3].translation, kGoal) < 1e-9);
    // Out of reach, straight at it.
    Modifier far = reaching();
    std::get<TwoBoneIk>(far.stage).goal = {5.0, 0.0, 1.0};
    const Pose kFar = modified(holding({far}));
    RAWFRAME_EXPECT(distance(kFar.bones[3].translation, {2.0, 0.0, 1.0}) < 1e-9);
    // Weighed nothing, the arm as the graph left it.
    const Pose kNone = modified(holding({reaching()}), false, 0.0);
    RAWFRAME_EXPECT(kNone.bones[3].translation == (std::array<double, 3>{2.0, 0.0, 1.0}));
    // Half weighed, somewhere between.
    const Pose kHalf = modified(holding({reaching()}), false, 0.5);
    RAWFRAME_EXPECT(distance(kHalf.bones[3].translation, kGoal) > 1e-3 &&
                    distance(kHalf.bones[3].translation, {2.0, 0.0, 1.0}) > 1e-3);
}

RAWFRAME_TEST(LookAtTurnsItsAxisToTheGoal) {
    // The shoulder's x axis turned toward (0, 1, 1): the arm points along y.
    const Pose kPose =
        modified(holding({Modifier{.stage = LookAt{.bone = kShoulder, .goal = {0.0, 5.0, 1.0}, .axis = {1, 0, 0}}}}));
    RAWFRAME_EXPECT(distance(kPose.bones[3].translation, {0.0, 2.0, 1.0}) < 1e-9);
    // Stages run in order: the look turns the arm, then the IK reaches from
    // there all the same.
    const Pose kBoth = modified(holding(
        {Modifier{.stage = LookAt{.bone = kShoulder, .goal = {0.0, 5.0, 1.0}, .axis = {1, 0, 0}}}, reaching()}));
    RAWFRAME_EXPECT(distance(kBoth.bones[3].translation, {1.0, 1.0, 1.0}) < 1e-9);
}

RAWFRAME_TEST(AServerRunsOnlySimulationStages) {
    // The look is presentation, the reach simulation.
    const Graph kGraph =
        holding({Modifier{.stage = LookAt{.bone = kShoulder, .goal = {0.0, 5.0, 1.0}, .axis = {1, 0, 0}}}});
    RAWFRAME_EXPECT(modified(kGraph, true).bones[3].translation == (std::array<double, 3>{2.0, 0.0, 1.0}));
    RAWFRAME_EXPECT(distance(modified(holding({reaching()}), true).bones[3].translation, {1.0, 1.0, 1.0}) < 1e-9);
}
