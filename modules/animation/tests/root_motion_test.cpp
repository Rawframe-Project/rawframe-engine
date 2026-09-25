// Root motion (SPEC-0035, D134): an advance says how its clips moved the
// character, across wraps and blends, in the character's own frame, and
// the pose is drawn with that motion taken out.

#include "rawframe/animation/instance.h"
#include "rawframe/test/test.h"

#include <cmath>
#include <memory>
#include <numbers>
#include <vector>

using namespace rawframe;
using namespace rawframe::animation;

namespace {

bool near(double a, double b) {
    return std::abs(a - b) <= 1e-9;
}

constexpr base::Bits128 kSkeletonId{7, 7};
constexpr base::Bits128 kRoot{1, 1};
constexpr base::Bits128 kStrollId{2, 1};
constexpr base::Bits128 kCircleId{2, 2};
constexpr base::Bits128 kRiseId{2, 3};

/// About z by `angle` radians.
std::array<double, 4> aboutZ(double angle) {
    return {0.0, 0.0, std::sin(angle / 2.0), std::cos(angle / 2.0)};
}

/// A root a meter up, taking its x and y and its turn about z.
Skeleton rig(bool rootMotion = true) {
    Skeleton made{
        .bones = {Bone{
            .target = kRoot, .name = "root", .parent = std::nullopt, .bind = Transform{.translation = {0, 0, 1}}}}};
    if (rootMotion) {
        made.rootMotion = RootMotionSource{.translation = {true, true, false}, .rotation = Axis::Z};
    }
    return made;
}

/// A second's loop: two meters along x a period, bobbing on z.
std::shared_ptr<const Clip> stroll() {
    return std::make_shared<const Clip>(
        Clip{.skeleton = kSkeletonId,
             .duration = 1.0,
             .loop = Loop::Loop,
             .tracks = {Track{.bone = kRoot,
                              .channel = Channel::Translation,
                              .keys = {Key{.time = 0.0, .value = {0, 0, 1}}, Key{.time = 0.5, .value = {1, 0, 1.25}}},
                              .drift = std::array<double, 4>{2, 0, 0, 0}}}});
}

/// A second's loop around a circle: two meters along x and a quarter turn
/// about z a period.
std::shared_ptr<const Clip> circle() {
    return std::make_shared<const Clip>(Clip{.skeleton = kSkeletonId,
                                             .duration = 1.0,
                                             .loop = Loop::Loop,
                                             .tracks = {Track{.bone = kRoot,
                                                              .channel = Channel::Translation,
                                                              .keys = {Key{.time = 0.0, .value = {0, 0, 1}}},
                                                              .drift = std::array<double, 4>{2, 0, 0, 0}},
                                                        Track{.bone = kRoot,
                                                              .channel = Channel::Rotation,
                                                              .keys = {Key{.time = 0.0, .value = aboutZ(0.0)}},
                                                              .drift = aboutZ(std::numbers::pi / 2.0)}}});
}

/// A clamped second rising a meter along y.
std::shared_ptr<const Clip> rise() {
    return std::make_shared<const Clip>(
        Clip{.skeleton = kSkeletonId,
             .duration = 1.0,
             .loop = Loop::Clamp,
             .tracks = {Track{.bone = kRoot,
                              .channel = Channel::Translation,
                              .keys = {Key{.time = 0.0, .value = {0, 0, 1}}, Key{.time = 1.0, .value = {0, 1, 1}}}}}});
}

/// A graph playing one clip.
std::shared_ptr<const CompiledGraph> playing(base::Bits128 clip, bool rootMotion = true) {
    const Graph kGraph{.parameters = {},
                       .nodes = {GraphNode{.id = 1, .node = ClipNode{.clip = clip}},
                                 GraphNode{.id = 2, .node = OutputNode{.pose = {.node = 1}}}},
                       .presentation = {}};
    const std::vector<NamedClip> kClips{{kStrollId, stroll()}, {kCircleId, circle()}, {kRiseId, rise()}};
    return *CompiledGraph::compile(kGraph, rig(rootMotion), kSkeletonId, kClips);
}

/// The stroll and the rise, half each.
std::shared_ptr<const CompiledGraph> blending() {
    const Graph kGraph{
        .parameters = {Parameter{.name = "mix", .id = 1, .initial = {0.5, 0.0}}},
        .nodes = {GraphNode{.id = 1, .node = ClipNode{.clip = kStrollId}},
                  GraphNode{.id = 2, .node = ClipNode{.clip = kRiseId}},
                  GraphNode{.id = 3,
                            .node = BlendNode{.inputs = {BlendInput{.name = "rise", .from = {.node = 2}, .weight = 0.5},
                                                         BlendInput{.name = "stroll",
                                                                    .from = {.node = 1},
                                                                    .weight = ParameterRef{1}}}}},
                  GraphNode{.id = 4, .node = OutputNode{.pose = {.node = 3}}}},
        .presentation = {}};
    const std::vector<NamedClip> kClips{{kStrollId, stroll()}, {kRiseId, rise()}};
    return *CompiledGraph::compile(kGraph, rig(), kSkeletonId, kClips);
}

/// The motion of one advance of `delta` seconds.
Transform advanced(GraphInstance& instance, double delta) {
    std::vector<GraphEvent> events;
    RAWFRAME_EXPECT(instance.advance(delta, events));
    return instance.rootMotion();
}

} // namespace

RAWFRAME_TEST(AnAdvanceSaysHowTheRootMoved) {
    GraphInstance strolling{playing(kStrollId)};
    // A quarter second: half way to the key a meter on; z is not taken.
    const Transform kQuarter = advanced(strolling, 0.25);
    RAWFRAME_EXPECT(near(kQuarter.translation[0], 0.5) && kQuarter.translation[1] == 0.0 &&
                    kQuarter.translation[2] == 0.0);
    // Across the wrap, the drift carries it on: from a quarter to a quarter
    // past the next period's start is a whole period, two meters.
    RAWFRAME_EXPECT(near(advanced(strolling, 1.0).translation[0], 2.0));
    // Backwards the same way.
    RAWFRAME_EXPECT(near(advanced(strolling, -1.5).translation[0], -3.0));
    // A clamped clip stops at its end.
    GraphInstance rising{playing(kRiseId)};
    RAWFRAME_EXPECT(near(advanced(rising, 0.5).translation[1], 0.5) && near(advanced(rising, 5.0).translation[1], 0.5));
    RAWFRAME_EXPECT(advanced(rising, 1.0) == Transform{});
    // A skeleton with no source has none.
    GraphInstance kept{playing(kStrollId, false)};
    RAWFRAME_EXPECT(advanced(kept, 0.25) == Transform{});
}

RAWFRAME_TEST(MotionIsInTheCharactersOwnFrame) {
    // A period: two meters ahead and a quarter turn. Two periods in one
    // advance: the second's two meters are along the first's turn.
    GraphInstance once{playing(kCircleId)};
    const Transform kPeriod = advanced(once, 1.0);
    RAWFRAME_EXPECT(near(kPeriod.translation[0], 2.0) && near(kPeriod.translation[1], 0.0));
    RAWFRAME_EXPECT(near(kPeriod.rotation[2], aboutZ(std::numbers::pi / 2.0)[2]));
    GraphInstance twice{playing(kCircleId)};
    const Transform kTwo = advanced(twice, 2.0);
    RAWFRAME_EXPECT(near(kTwo.translation[0], 2.0) && near(kTwo.translation[1], 2.0));
    RAWFRAME_EXPECT(near(kTwo.rotation[2], 1.0) && near(kTwo.rotation[3], 0.0));
    // Four advances of a quarter each arrive where one of a second does.
    GraphInstance quarters{playing(kCircleId)};
    Transform walked;
    for (int step = 0; step < 4; ++step) {
        const Transform kStep = advanced(quarters, 0.25);
        const double kCos = (walked.rotation[3] * walked.rotation[3]) - (walked.rotation[2] * walked.rotation[2]);
        const double kSin = 2.0 * walked.rotation[2] * walked.rotation[3];
        walked.translation[0] += (kCos * kStep.translation[0]) - (kSin * kStep.translation[1]);
        walked.translation[1] += (kSin * kStep.translation[0]) + (kCos * kStep.translation[1]);
        const double kZ = (walked.rotation[3] * kStep.rotation[2]) + (walked.rotation[2] * kStep.rotation[3]);
        walked.rotation[3] = (walked.rotation[3] * kStep.rotation[3]) - (walked.rotation[2] * kStep.rotation[2]);
        walked.rotation[2] = kZ;
    }
    RAWFRAME_EXPECT(near(walked.translation[0], kPeriod.translation[0]) &&
                    near(walked.translation[1], kPeriod.translation[1]));
    RAWFRAME_EXPECT(near(walked.rotation[2], kPeriod.rotation[2]));
}

RAWFRAME_TEST(MotionBlendsAsPosesDo) {
    // Half the stroll's half meter and half the rise's quarter.
    GraphInstance instance{blending()};
    const Transform kMoved = advanced(instance, 0.25);
    RAWFRAME_EXPECT(near(kMoved.translation[0], 0.25) && near(kMoved.translation[1], 0.125));
}

RAWFRAME_TEST(ThePoseIsDrawnWithItsMotionTakenOut) {
    GraphInstance instance{playing(kCircleId)};
    std::vector<GraphEvent> events;
    RAWFRAME_EXPECT(instance.advance(0.5, events));
    PoseEvaluator evaluator;
    Pose pose;
    evaluator.evaluate(instance, pose);
    // A meter along x and an eighth turn in the clip; the root stays where
    // the bind is, keeping the z it was not asked to give.
    RAWFRAME_EXPECT(near(pose.bones[0].translation[0], 1.0) &&
                    near(pose.bones[0].rotation[2], aboutZ(0.785398163397448)[2]));
    removeRootMotion(instance.graph(), pose);
    RAWFRAME_EXPECT(pose.bones[0].translation == (std::array<double, 3>{0, 0, 1}));
    RAWFRAME_EXPECT(near(pose.bones[0].rotation[2], 0.0) && near(pose.bones[0].rotation[3], 1.0));
    // Without a source the pose keeps it.
    GraphInstance kept{playing(kCircleId, false)};
    RAWFRAME_EXPECT(kept.advance(0.5, events));
    evaluator.evaluate(kept, pose);
    removeRootMotion(kept.graph(), pose);
    RAWFRAME_EXPECT(near(pose.bones[0].translation[0], 1.0));
}
