// Additive layers (SPEC-0035, D137): clips of differences from a declared
// basis, added onto a base pose by weight, and poses and differences never
// mixed.

#include "rawframe/animation/errors.h"
#include "rawframe/animation/instance.h"
#include "rawframe/test/test.h"

#include <cmath>
#include <memory>
#include <numbers>
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
constexpr base::Bits128 kStandId{2, 1};
constexpr base::Bits128 kLeanId{2, 2};
constexpr std::uint64_t kLean = 0x6a00000000000001ULL;

/// About z by `angle` radians.
std::array<double, 4> aboutZ(double angle) {
    return {0.0, 0.0, std::sin(angle / 2.0), std::cos(angle / 2.0)};
}

Skeleton rig() {
    return Skeleton{.bones = {Bone{.target = kRoot, .name = "root", .parent = std::nullopt, .bind = {}}}};
}

/// Standing a meter along x.
std::shared_ptr<const Clip> stand() {
    return std::make_shared<const Clip>(
        Clip{.skeleton = kSkeletonId,
             .duration = 1.0,
             .loop = Loop::Loop,
             .tracks = {Track{.bone = kRoot, .channel = Channel::Translation, .keys = {Key{.value = {1, 0, 0}}}}}});
}

/// A lean: a meter up, a quarter turn about z, and twice as tall, as
/// differences from the bind.
std::shared_ptr<const Clip> lean() {
    const std::array<double, 4> kQuarter = aboutZ(std::numbers::pi / 2.0);
    return std::make_shared<const Clip>(
        Clip{.skeleton = kSkeletonId,
             .duration = 1.0,
             .loop = Loop::Loop,
             .additive = AdditiveBasis::Bind,
             .tracks = {Track{.bone = kRoot, .channel = Channel::Translation, .keys = {Key{.value = {0, 1, 0}}}},
                        Track{.bone = kRoot, .channel = Channel::Rotation, .keys = {Key{.value = kQuarter}}},
                        Track{.bone = kRoot, .channel = Channel::Scale, .keys = {Key{.value = {2, 2, 2}}}}}});
}

/// The stand with the lean added by `lean`.
Graph leaning() {
    return Graph{.parameters = {Parameter{.name = "lean", .id = kLean, .initial = {0.5, 0.0}}},
                 .nodes = {GraphNode{.id = 1, .node = ClipNode{.clip = kStandId}},
                           GraphNode{.id = 2, .node = ClipNode{.clip = kLeanId}},
                           GraphNode{.id = 3,
                                     .node = AdditiveNode{.base = {.node = 1},
                                                          .layers = {BlendInput{.name = "lean",
                                                                                .from = {.node = 2},
                                                                                .weight = ParameterRef{kLean}}}}},
                           GraphNode{.id = 4, .node = OutputNode{.pose = {.node = 3}}}},
                 .presentation = {}};
}

result::Result<std::shared_ptr<const CompiledGraph>> compiled(const Graph& graph) {
    const std::vector<NamedClip> kClips{{kStandId, stand()}, {kLeanId, lean()}};
    return CompiledGraph::compile(graph, rig(), kSkeletonId, kClips);
}

} // namespace

RAWFRAME_TEST(AnAdditiveClipDeclaresItsBasis) {
    const auto kText = writeClip(*lean());
    RAWFRAME_EXPECT(kText.has_value() && kText->contains("\"loop\": \"loop\",\n  \"additive\": \"bind\",\n"));
    RAWFRAME_EXPECT(kText.has_value() && readClip(*kText) == *lean());
    Clip first = *lean();
    first.additive = AdditiveBasis::FirstFrame;
    const auto kFirst = writeClip(first);
    RAWFRAME_EXPECT(kFirst.has_value() && kFirst->contains("\"first_frame\"") && readClip(*kFirst) == first);
    std::string unknown = *kText;
    unknown.replace(unknown.find("\"bind\""), 6, "\"rest\"");
    RAWFRAME_EXPECT(refusedWith(readClip(unknown), AnimationError::ClipInvalid));
    // Differences carry no root motion, so none drifts.
    Clip drifting = *lean();
    drifting.tracks[0].drift = std::array<double, 4>{1, 0, 0, 0};
    RAWFRAME_EXPECT(refusedWith(writeClip(drifting), AnimationError::ClipInvalid));
}

RAWFRAME_TEST(AnAdditiveNodeHasOneText) {
    const auto kText = writeGraph(leaning());
    RAWFRAME_EXPECT(kText.has_value() && readGraph(*kText) == leaning());
    RAWFRAME_EXPECT(kText.has_value() && kText->contains("\"type\": \"rawframe/additive@1\""));
    // Its layers are named other than base, and it has one.
    Graph based = leaning();
    std::get<AdditiveNode>(based.nodes[2].node).layers[0].name = "base";
    Graph bare = leaning();
    std::get<AdditiveNode>(bare.nodes[2].node).layers.clear();
    for (const Graph& kGraph : {based, bare}) {
        RAWFRAME_EXPECT(refusedWith(writeGraph(kGraph), AnimationError::GraphInvalid));
    }
    // A weighed base is no text of one.
    std::string weighed = *kText;
    weighed.replace(weighed.find("\"weights\": {\n"), 13, "\"weights\": {\n          \"base\": 2,\n");
    RAWFRAME_EXPECT(refusedWith(readGraph(weighed), AnimationError::GraphInvalid));
}

RAWFRAME_TEST(LayersAddTheirDifferencesByWeight) {
    GraphInstance instance{*compiled(leaning())};
    std::vector<GraphEvent> events;
    RAWFRAME_EXPECT(instance.advance(0.0, events));
    PoseEvaluator evaluator;
    Pose pose;
    evaluator.evaluate(instance, pose);
    // Half the lean: half a meter up, an eighth turn, half again as tall.
    const Transform& root = pose.bones[0];
    RAWFRAME_EXPECT(near(root.translation[0], 1.0) && near(root.translation[1], 0.5));
    RAWFRAME_EXPECT(near(root.rotation[2], aboutZ(std::numbers::pi / 4.0)[2]) &&
                    near(root.rotation[3], aboutZ(std::numbers::pi / 4.0)[3]));
    RAWFRAME_EXPECT(near(root.scale[0], 1.5));
    // Weighed nothing, the stand alone.
    RAWFRAME_EXPECT(instance.set(ParameterIndex{0}, {0.0, 0.0}).has_value());
    RAWFRAME_EXPECT(instance.advance(0.0, events));
    evaluator.evaluate(instance, pose);
    RAWFRAME_EXPECT(pose.bones[0].translation == (std::array<double, 3>{1, 0, 0}) &&
                    pose.bones[0].scale == (std::array<double, 3>{1, 1, 1}));
}

RAWFRAME_TEST(PosesAndDifferencesNeverMix) {
    // A pose as a layer, differences as the base, differences at the output,
    // and a blend of one of each.
    Graph layered = leaning();
    std::swap(std::get<AdditiveNode>(layered.nodes[2].node).base.node,
              std::get<AdditiveNode>(layered.nodes[2].node).layers[0].from.node);
    Graph bare = leaning();
    std::get<OutputNode>(bare.nodes[3].node).pose.node = 2;
    Graph mixed = leaning();
    mixed.nodes[2].node = BlendNode{
        .inputs = {BlendInput{.name = "lean", .from = {.node = 2}}, BlendInput{.name = "stand", .from = {.node = 1}}}};
    for (const Graph& kGraph : {layered, bare, mixed}) {
        RAWFRAME_EXPECT(refusedWith(compiled(kGraph), AnimationError::GraphInvalid));
    }
    RAWFRAME_EXPECT(compiled(leaning()).has_value());
}
