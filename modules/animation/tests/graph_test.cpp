// Animation graphs: one canonical text on SPEC-0028's grammar, unknown
// node types kept whole, a semantic hash blind to ids and drawings, and
// every rule held against hostile documents.

#include "rawframe/animation/errors.h"
#include "rawframe/animation/graph.h"
#include "rawframe/test/mutations.h"
#include "rawframe/test/test.h"

#include <algorithm>
#include <string>
#include <string_view>

using namespace rawframe;
using namespace rawframe::animation;

namespace {

bool refusedWith(const auto& outcome, AnimationError error) {
    return !outcome.has_value() && outcome.error().domain() == kAnimationDomain &&
           outcome.error().code() == code(error);
}

constexpr base::Bits128 kIdle{2, 1};
constexpr base::Bits128 kWalk{2, 2};
constexpr std::uint64_t kSpeed = 0x5f3a0c2d9e81b746ULL;
constexpr std::uint64_t kMove = 0x6a00000000000001ULL;

/// Idle and walk blended by how much the character moves, the walk
/// played as fast as it goes.
Graph locomotion(std::uint64_t idle = 0x1f00000000000001ULL,
                 std::uint64_t walk = 0x1f00000000000002ULL,
                 std::uint64_t blend = 0x3f00000000000003ULL,
                 std::uint64_t output = 0x4f00000000000004ULL) {
    Graph graph{
        .parameters =
            {Parameter{.name = "aim",
                       .id = 0x7a00000000000001ULL,
                       .type = ParameterType::Vec2,
                       .initial = {0.5, -0.25},
                       .replication = Replication::Local},
             Parameter{.name = "grounded",
                       .id = 0x7a00000000000002ULL,
                       .type = ParameterType::Bool,
                       .initial = {1.0, 0.0},
                       .replication = Replication::ClientPredicted},
             Parameter{.name = "move", .id = kMove, .type = ParameterType::Float, .minimum = 0.0, .maximum = 1.0},
             Parameter{.name = "speed", .id = kSpeed, .type = ParameterType::Float, .minimum = 0.0, .maximum = 8.0},
             Parameter{.name = "stance",
                       .id = 0x7a00000000000003ULL,
                       .type = ParameterType::Int,
                       .initial = {2.0, 0.0},
                       .minimum = -3.0}},
        .nodes = {GraphNode{.id = idle, .node = ClipNode{.clip = kIdle}},
                  GraphNode{.id = walk,
                            .node = ClipNode{.clip = kWalk, .speed = ParameterRef{kSpeed}, .loop = Loop::Loop}},
                  GraphNode{.id = blend,
                            .node = BlendNode{.inputs = {BlendInput{.name = "idle", .from = {.node = idle}},
                                                         BlendInput{.name = "walk",
                                                                    .from = {.node = walk},
                                                                    .weight = ParameterRef{kMove}}}}},
                  GraphNode{.id = output, .node = OutputNode{.pose = {.node = blend}}}},
        .presentation = {}};
    std::ranges::sort(graph.nodes, {}, &GraphNode::id);
    return graph;
}

} // namespace

RAWFRAME_TEST(AGraphHasOneText) {
    Graph drawn = locomotion();
    drawn.presentation = {{0x1f00000000000001ULL, "{\"x\":-120,\"y\":40}"}};
    const auto kText = writeGraph(drawn);
    RAWFRAME_EXPECT(kText.has_value());
    if (!kText.has_value()) {
        return;
    }
    // Changing the form moves this, and needs a new format version.
    RAWFRAME_EXPECT(kText->starts_with("{\n  \"formatVersion\": 1,\n  \"kind\": \"animation.graph\",\n  \"interface\": "
                                       "{\n    \"parameters\": {\n      \"aim\": {\n        \"parameter\": "
                                       "\"7a00000000000001\",\n        \"type\": \"vec2\",\n        \"default\": [\n"));
    RAWFRAME_EXPECT(kText->contains("\"grounded\": {\n        \"parameter\": \"7a00000000000002\",\n        \"type\": "
                                    "\"bool\",\n        \"default\": true,\n        \"replication\": "
                                    "\"client_predicted\"\n      },"));
    RAWFRAME_EXPECT(kText->contains("\"type\": \"int\",\n        \"default\": 2,\n        \"minimum\": -3,\n"));
    RAWFRAME_EXPECT(kText->contains("\"1f00000000000001\": {\n      \"type\": \"rawframe/clip@1\",\n      \"params\": "
                                    "{\n        \"clip\": \"00000000000000020000000000000001\"\n      },\n      "
                                    "\"inputs\": {}\n    },"));
    RAWFRAME_EXPECT(kText->contains("\"params\": {\n        \"weights\": {\n          \"walk\": {\n            "
                                    "\"parameter\": \"6a00000000000001\"\n          }\n        }\n      },"));
    RAWFRAME_EXPECT(kText->ends_with("\"presentation\": {\n    \"1f00000000000001\": {\n      \"x\": -120,\n      "
                                     "\"y\": 40\n    }\n  }\n}\n"));
    const auto kRead = readGraph(*kText);
    RAWFRAME_EXPECT(kRead.has_value() && *kRead == drawn);
}

RAWFRAME_TEST(AnUnknownNodeIsKeptWholeAndNeverPlayed) {
    Graph graph = locomotion();
    const std::string kRecord = "{\"type\":\"studio/nodes/ragdoll@2.1\",\"params\":{\"stiffness\":0.5,\"chains\":["
                                "\"arm\",\"leg\"]},\"inputs\":{\"source\":{\"node\":\"3f00000000000003\",\"output\":"
                                "\"pose\"}},\"notes\":\"from a newer engine\"}";
    graph.nodes.push_back(GraphNode{.id = 0x5f00000000000005ULL,
                                    .node = QuarantinedNode{.type = "studio/nodes/ragdoll@2.1", .record = kRecord}});
    std::get<OutputNode>(graph.nodes[3].node).pose = {.node = 0x5f00000000000005ULL, .output = "limp"};
    const auto kText = writeGraph(graph);
    RAWFRAME_EXPECT(kText.has_value() && kText->contains("\"notes\": \"from a newer engine\""));
    if (!kText.has_value()) {
        return;
    }
    const auto kRead = readGraph(*kText);
    RAWFRAME_EXPECT(kRead.has_value() && *kRead == graph && writeGraph(*kRead) == *kText);
    RAWFRAME_EXPECT(refusedWith(semanticHash(graph), AnimationError::GraphInvalid));
    // An unknown version of a known type is as unknown.
    std::string newer = *kText;
    newer.replace(newer.find("rawframe/clip@1"), 15, "rawframe/clip@2");
    const auto kNewer = readGraph(newer);
    RAWFRAME_EXPECT(kNewer.has_value() && std::holds_alternative<QuarantinedNode>(kNewer->nodes[0].node));
}

RAWFRAME_TEST(TheSemanticHashIsTheMeaningAlone) {
    const auto kHash = semanticHash(locomotion());
    RAWFRAME_EXPECT(kHash.has_value());
    // Other ids and another order of them, a drawing, and a node nothing
    // reaches: the same meaning.
    const auto kRenamed = semanticHash(
        locomotion(0x9000000000000009ULL, 0x0100000000000001ULL, 0x0200000000000002ULL, 0x0300000000000003ULL));
    Graph drawn = locomotion();
    drawn.presentation = {{0x3f00000000000003ULL, "{\"collapsed\":true}"}};
    Graph stray = locomotion();
    stray.nodes.push_back(GraphNode{.id = 0xff00000000000000ULL, .node = ClipNode{.clip = kIdle}});
    const auto kSame = [&kHash](const auto& other) {
        return kHash.has_value() && other.has_value() && *kHash == *other;
    };
    RAWFRAME_EXPECT(kSame(kRenamed) && kSame(semanticHash(drawn)) && kSame(semanticHash(stray)));
    // Any change of meaning moves it.
    std::vector<Graph> changed(5, locomotion());
    std::get<ClipNode>(changed[0].nodes[1].node).speed = 1.5;
    std::get<ClipNode>(changed[1].nodes[0].node).loop = Loop::Clamp;
    std::get<BlendNode>(changed[2].nodes[2].node).inputs[0].weight = 0.5;
    changed[3].parameters[3].maximum = 9.0;
    std::swap(std::get<BlendNode>(changed[4].nodes[2].node).inputs[0].from,
              std::get<BlendNode>(changed[4].nodes[2].node).inputs[1].from);
    for (const Graph& kGraph : changed) {
        const auto kOther = semanticHash(kGraph);
        RAWFRAME_EXPECT(kOther.has_value() && kHash.has_value() && *kOther != *kHash);
    }
}

RAWFRAME_TEST(AGraphOutOfItsRulesIsRefused) {
    std::vector<Graph> wrong(20, locomotion());
    wrong[0].nodes.push_back(
        GraphNode{.id = 0xf000000000000000ULL, .node = OutputNode{.pose = {.node = 0x3f00000000000003ULL}}});
    wrong[1].nodes.erase(wrong[1].nodes.begin() + 3);
    std::get<OutputNode>(wrong[2].nodes[3].node).pose.node = 0x1234;
    std::get<OutputNode>(wrong[3].nodes[3].node).pose.output = "limp";
    // The blend feeding itself through a second blend.
    wrong[4].nodes.push_back(
        GraphNode{.id = 0x3f00000000000004ULL,
                  .node = BlendNode{.inputs = {BlendInput{.name = "loop", .from = {.node = 0x3f00000000000003ULL}}}}});
    std::get<BlendNode>(wrong[4].nodes[2].node).inputs[0].from.node = 0x3f00000000000004ULL;
    std::ranges::sort(wrong[4].nodes, {}, &GraphNode::id);
    std::get<ClipNode>(wrong[5].nodes[1].node).speed = ParameterRef{0x7a00000000000003ULL};
    std::get<ClipNode>(wrong[6].nodes[1].node).speed = ParameterRef{0x99};
    std::get<BlendNode>(wrong[7].nodes[2].node).inputs[0].weight = -0.5;
    std::swap(wrong[8].parameters[0], wrong[8].parameters[1]);
    wrong[9].parameters[1].id = kSpeed;
    wrong[10].parameters[1].initial = {2.0, 0.0};
    wrong[11].parameters[4].initial = {1.5, 0.0};
    wrong[12].parameters[0].minimum = 0.0;
    wrong[13].parameters[4].initial = {-4.0, 0.0};
    wrong[14].parameters[2].name = "Move";
    std::get<BlendNode>(wrong[15].nodes[2].node).inputs.clear();
    std::get<ClipNode>(wrong[16].nodes[0].node).clip = {};
    wrong[17].presentation = {{0x42, "{}"}};
    wrong[18].nodes[0].id = 0;
    std::swap(wrong[19].nodes[0], wrong[19].nodes[1]);
    for (const Graph& kGraph : wrong) {
        RAWFRAME_EXPECT(refusedWith(writeGraph(kGraph), AnimationError::GraphInvalid));
    }
    RAWFRAME_EXPECT(refusedWith(writeGraph(locomotion(), {.maximumNodes = 3}), AnimationError::OverLimit));
    RAWFRAME_EXPECT(refusedWith(writeGraph(locomotion(), {.maximumInputs = 1}), AnimationError::OverLimit));
    RAWFRAME_EXPECT(refusedWith(writeGraph(locomotion(), {.maximumParameters = 4}), AnimationError::OverLimit));
}

RAWFRAME_TEST(OnlyTheCanonicalGraphTextReads) {
    const auto kText = writeGraph(locomotion());
    RAWFRAME_EXPECT(kText.has_value());
    if (!kText.has_value()) {
        return;
    }
    const auto kReplaced = [&kText](std::string_view from, std::string_view to) {
        std::string made = *kText;
        made.replace(made.find(from), from.size(), to);
        return made;
    };
    for (const std::string& kWrong : {
             // A param at its default is left out.
             kReplaced("\"clip\": \"00000000000000020000000000000001\"\n",
                       "\"clip\": \"00000000000000020000000000000001\",\n        \"speed\": 1\n"),
             kReplaced("rawframe/output@1", "rawframe/output"),
             kReplaced("rawframe/output@1", "rawframe/output@01"),
             kReplaced("rawframe/output@1", "Rawframe/output@1"),
             kReplaced("rawframe/output@1", "a/b/c/output@1"),
             kReplaced("\"formatVersion\": 1", "\"formatVersion\": 2"),
             kReplaced("\"type\": \"bool\"", "\"type\": \"boolean\""),
             kReplaced("\"replication\": \"local\"", "\"replication\": \"everyone\""),
             kReplaced("\"default\": true", "\"default\": 1"),
             kReplaced("\"default\": 2", "\"default\": 2.0"),
             kReplaced("\"1f00000000000001\": {", "\"1F00000000000001\": {"),
             kReplaced("\"output\": \"pose\"", "\"output\": 0"),
             kReplaced("\"inputs\": {}", "\"inputs\": {},\n      \"extra\": 1"),
         }) {
        RAWFRAME_EXPECT(!readGraph(kWrong).has_value());
    }
    for (std::size_t length = 0; length < kText->size(); ++length) {
        RAWFRAME_EXPECT(!readGraph(std::string_view{*kText}.substr(0, length)).has_value());
    }
    RAWFRAME_EXPECT(refusedWith(readGraph(*kText, {.maximumNodes = 3}), AnimationError::OverLimit));
    RAWFRAME_EXPECT(refusedWith(readGraph(*kText, {.maximumParameters = 4}), AnimationError::OverLimit));
}

RAWFRAME_TEST(HostileGraphsReadOnlyAsTheyWrite) {
    const auto kText = writeGraph(locomotion());
    RAWFRAME_EXPECT(kText.has_value());
    if (!kText.has_value()) {
        return;
    }
    const test::WrittenRun kRun = test::readOnlyAsWritten(
        *kText,
        "\"{}[],:.-+eE0123456789 \n",
        [](std::string_view text) {
            return readGraph(text);
        },
        [](const Graph& read) {
            return writeGraph(read);
        });
    RAWFRAME_EXPECT(kRun.read > 0 && kRun.differing == 0);
}
