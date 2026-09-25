// Blend spaces (SPEC-0035, D135): points on a line or a plane as the graph
// document holds them, a declared triangulation checked where it is read,
// and weights shared by where the position is among the points.

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
    return std::abs(a - b) <= 1e-12;
}

constexpr base::Bits128 kSkeletonId{7, 7};
constexpr base::Bits128 kRoot{1, 1};
constexpr std::uint64_t kSpeed = 0x6a00000000000001ULL;
constexpr std::uint64_t kHeading = 0x6a00000000000002ULL;

Skeleton rig() {
    return Skeleton{.bones = {Bone{.target = kRoot, .name = "root", .parent = std::nullopt, .bind = {}}}};
}

/// A clip holding the root at `x` along x and `y` along y.
NamedClip holding(std::uint64_t low, double x, double y) {
    return NamedClip{
        .id = base::Bits128{2, low},
        .clip = std::make_shared<const Clip>(
            Clip{.skeleton = kSkeletonId,
                 .duration = 1.0,
                 .loop = Loop::Loop,
                 .tracks = {Track{
                     .bone = kRoot, .channel = Channel::Translation, .keys = {Key{.value = {x, y, 0.0, 0.0}}}}}})};
}

/// Clip nodes 1 to `count`, each holding its clip `2, n`.
std::vector<GraphNode> clipNodes(std::uint64_t count) {
    std::vector<GraphNode> made;
    for (std::uint64_t at = 1; at <= count; ++at) {
        made.push_back(GraphNode{.id = at, .node = ClipNode{.clip = base::Bits128{2, at}}});
    }
    return made;
}

/// Idle at 0, walk at 1.5, run at 4, by `speed`.
Graph line() {
    Graph made{.parameters = {Parameter{.name = "speed", .id = kSpeed}}, .nodes = clipNodes(3), .presentation = {}};
    made.nodes.push_back(GraphNode{
        .id = 0x10,
        .node = BlendSpace1DNode{.position = ParameterRef{kSpeed},
                                 .points = {BlendSpacePoint{.name = "idle", .from = {.node = 1}, .at = {0, 0}},
                                            BlendSpacePoint{.name = "run", .from = {.node = 3}, .at = {4, 0}},
                                            BlendSpacePoint{.name = "walk", .from = {.node = 2}, .at = {1.5, 0}}}}});
    made.nodes.push_back(GraphNode{.id = 0x20, .node = OutputNode{.pose = {.node = 0x10}}});
    return made;
}

/// A square of four points about the origin, split along one diagonal, by
/// `heading`.
Graph plane() {
    Graph made{.parameters = {Parameter{.name = "heading", .id = kHeading, .type = ParameterType::Vec2}},
               .nodes = clipNodes(4),
               .presentation = {}};
    made.nodes.push_back(GraphNode{
        .id = 0x10,
        .node = BlendSpace2DNode{.position = ParameterRef{kHeading},
                                 .points = {BlendSpacePoint{.name = "back", .from = {.node = 1}, .at = {0, -1}},
                                            BlendSpacePoint{.name = "left", .from = {.node = 2}, .at = {-1, 0}},
                                            BlendSpacePoint{.name = "right", .from = {.node = 3}, .at = {1, 0}},
                                            BlendSpacePoint{.name = "up", .from = {.node = 4}, .at = {0, 1}}},
                                 .triangles = {{"back", "left", "right"}, {"left", "right", "up"}}}});
    made.nodes.push_back(GraphNode{.id = 0x20, .node = OutputNode{.pose = {.node = 0x10}}});
    return made;
}

/// The root's x and y after a first advance at `value`.
std::array<double, 2> posedAt(const Graph& graph, ParameterValue value) {
    const std::vector<NamedClip> kClips{
        holding(1, 0.0, 0.0), holding(2, 1.0, 0.0), holding(3, 2.0, 0.0), holding(4, 0.0, 1.0)};
    GraphInstance instance{*CompiledGraph::compile(graph, rig(), kSkeletonId, kClips)};
    RAWFRAME_EXPECT(instance.set(ParameterIndex{0}, value).has_value());
    std::vector<GraphEvent> events;
    RAWFRAME_EXPECT(instance.advance(0.0, events));
    PoseEvaluator evaluator;
    Pose pose;
    evaluator.evaluate(instance, pose);
    return {pose.bones[0].translation[0], pose.bones[0].translation[1]};
}

} // namespace

RAWFRAME_TEST(BlendSpacesHaveOneText) {
    for (const Graph& kGraph : {line(), plane()}) {
        const auto kText = writeGraph(kGraph);
        RAWFRAME_EXPECT(kText.has_value() && readGraph(*kText) == kGraph);
    }
    const auto kLine = writeGraph(line());
    RAWFRAME_EXPECT(kLine.has_value() && kLine->contains("\"type\": \"rawframe/blend_space_1d@1\",\n      \"params\": "
                                                         "{\n        \"points\": {\n          \"idle\": 0,"));
    const auto kPlane = writeGraph(plane());
    RAWFRAME_EXPECT(kPlane.has_value() && kPlane->contains("\"triangles\": [\n          [\n            \"back\","));
    // At its default, the position is left out.
    Graph still = line();
    std::get<BlendSpace1DNode>(still.nodes[3].node).position = 0.0;
    const auto kStill = writeGraph(still);
    RAWFRAME_EXPECT(kStill.has_value() && !kStill->contains("position") && readGraph(*kStill) == still);
}

RAWFRAME_TEST(ABlendSpaceOutOfItsRulesIsRefused) {
    std::vector<Graph> wrong(4, line());
    std::get<BlendSpace1DNode>(wrong[0].nodes[3].node).points[2].at = {4, 0};
    std::get<BlendSpace1DNode>(wrong[1].nodes[3].node).points.clear();
    std::get<BlendSpace1DNode>(wrong[2].nodes[3].node).points[0].at = {std::nan(""), 0};
    std::ranges::swap(std::get<BlendSpace1DNode>(wrong[3].nodes[3].node).points[0],
                      std::get<BlendSpace1DNode>(wrong[3].nodes[3].node).points[1]);
    const auto kPlaneOf = [](Graph& graph) -> BlendSpace2DNode& {
        return std::get<BlendSpace2DNode>(graph.nodes[4].node);
    };
    for (std::size_t at = 0; at < 8; ++at) {
        wrong.push_back(plane());
    }
    // Flat, overlapping, a point in no triangle, out of order, a name it
    // lacks, none at all, a float position, and too few points.
    kPlaneOf(wrong[4]).points[3].at = {2, 1};
    kPlaneOf(wrong[4]).triangles = {{"back", "left", "right"}, {"back", "right", "up"}};
    kPlaneOf(wrong[5]).triangles = {{"back", "left", "right"}, {"back", "left", "up"}};
    kPlaneOf(wrong[6]).triangles = {{"back", "left", "right"}};
    kPlaneOf(wrong[7]).triangles = {{"left", "right", "up"}, {"back", "left", "right"}};
    kPlaneOf(wrong[8]).triangles[1][2] = "zed";
    kPlaneOf(wrong[9]).triangles.clear();
    wrong[10].parameters[0].type = ParameterType::Float;
    kPlaneOf(wrong[11]).points.resize(2);
    kPlaneOf(wrong[11]).triangles.clear();
    for (const Graph& kGraph : wrong) {
        RAWFRAME_EXPECT(refusedWith(writeGraph(kGraph), AnimationError::GraphInvalid));
    }
    RAWFRAME_EXPECT(refusedWith(writeGraph(plane(), {.maximumTriangles = 1}), AnimationError::OverLimit));
    RAWFRAME_EXPECT(refusedWith(writeGraph(line(), {.maximumInputs = 2}), AnimationError::OverLimit));
}

RAWFRAME_TEST(ALineSharesBetweenTheTwoAboutThePosition) {
    // Idle holds 0, walk 1, run 2 along x.
    RAWFRAME_EXPECT(near(posedAt(line(), {0.75, 0})[0], 0.5));
    RAWFRAME_EXPECT(near(posedAt(line(), {2.75, 0})[0], 1.5));
    RAWFRAME_EXPECT(posedAt(line(), {1.5, 0})[0] == 1.0);
    RAWFRAME_EXPECT(posedAt(line(), {-3, 0})[0] == 0.0 && posedAt(line(), {9, 0})[0] == 2.0);
}

RAWFRAME_TEST(APlaneSharesWithinItsTriangles) {
    // Back holds (0, 0), left (1, 0), right (2, 0), up (0, 1).
    const auto kAt = [](double x, double y) {
        return posedAt(plane(), {x, y});
    };
    // At the origin, on the shared edge: half left, half right, from the
    // first triangle.
    RAWFRAME_EXPECT(near(kAt(0, 0)[0], 1.5) && near(kAt(0, 0)[1], 0.0));
    // A quarter up from the origin: a quarter of up, the rest shared by
    // left and right.
    RAWFRAME_EXPECT(near(kAt(0, 0.25)[0], 1.125) && near(kAt(0, 0.25)[1], 0.25));
    // On a point, all of it.
    RAWFRAME_EXPECT(near(kAt(0, -1)[0], 0.0) && near(kAt(0, -1)[1], 0.0));
    // Outside: the nearest point of the nearest edge, the right-up edge's
    // middle.
    RAWFRAME_EXPECT(near(kAt(2, 2)[0], 1.0) && near(kAt(2, 2)[1], 0.5));
    // Past a corner: the corner.
    RAWFRAME_EXPECT(near(kAt(5, 0)[0], 2.0) && near(kAt(5, 0)[1], 0.0));
}
