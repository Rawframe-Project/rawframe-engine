// Graph documents: SPEC-0028's grammar read and written in one form, every
// rule held against hostile text, and a semantic hash blind to ids, order,
// and drawings but not to meaning.

#include "rawframe/graph/errors.h"
#include "rawframe/graph/graph.h"
#include "rawframe/test/test.h"

#include <algorithm>
#include <array>
#include <string>
#include <string_view>

using namespace rawframe;
using namespace rawframe::graph;
using document::Value;

namespace {

bool refusedWith(const auto& outcome, GraphError error) {
    return !outcome.has_value() && outcome.error().domain() == kGraphDomain && outcome.error().code() == code(error);
}

Value record(std::string_view type, Value inputs = Value::object(), Value params = Value::object()) {
    Value made = Value::object();
    made.add("type", Value::string(std::string{type}));
    made.add("params", std::move(params));
    made.add("inputs", std::move(inputs));
    return made;
}

/// A constant feeding a surface, with an unknown node beside them.
Document sample(NodeId constant = 0x1f00000000000001ULL, NodeId surface = 0x2f00000000000002ULL) {
    Value params = Value::object();
    params.add("value", Value::real(0.5));
    Value inputs = Value::object();
    inputs.add("base_metalness", connectionValue({.node = constant, .output = "out"}));
    inputs.add("specular_roughness", Value::real(0.25));
    Document made{.kind = "test.graph"};
    made.nodes = {{.id = constant, .record = record("rawframe/constant@1", Value::object(), std::move(params))},
                  {.id = surface, .record = record("rawframe/surface@1", std::move(inputs))},
                  {.id = 0x3f00000000000003ULL, .record = record("studio/nodes/glow@2.1")}};
    std::ranges::sort(made.nodes, {}, &Node::id);
    made.sections.emplace_back("states", Value::object());
    Value drawing = Value::object();
    drawing.add("x", Value::integer(40));
    made.presentation.emplace_back(surface, std::move(drawing));
    return made;
}

} // namespace

RAWFRAME_TEST(IdsAndTypesAreInForm) {
    RAWFRAME_EXPECT(nodeIdText(0x1fULL) == "000000000000001f" && nodeIdOf("000000000000001f") == 0x1fULL);
    RAWFRAME_EXPECT(!nodeIdOf("000000000000001F").has_value() && !nodeIdOf("1f").has_value());
    for (const std::string_view kGood : {"rawframe/clip@1", "rawframe/clip@1.2", "studio/nodes/ragdoll@2"}) {
        RAWFRAME_EXPECT(typeIdInForm(kGood));
    }
    // Two parts are the standard library's alone; a version is required,
    // without leading zeros.
    for (const std::string_view kBad :
         {"studio/ragdoll@1", "rawframe/clip", "rawframe/clip@01", "rawframe/Clip@1", "a/b/c/d@1", "rawframe/@1"}) {
        RAWFRAME_EXPECT(!typeIdInForm(kBad));
    }
    RAWFRAME_EXPECT(connectionOf(connectionValue({.node = 7, .output = "pose"})) ==
                    (Connection{.node = 7, .output = "pose"}));
    RAWFRAME_EXPECT(!connectionOf(Value::real(1)).has_value());
}

RAWFRAME_TEST(ADocumentReadsBackAsItWasWritten) {
    const Document kMade = sample();
    const auto kText = writeDocument(kMade);
    RAWFRAME_EXPECT(kText.has_value());
    if (!kText.has_value()) {
        return;
    }
    constexpr std::array<std::string_view, 1> kSections = {"states"};
    const auto kRead = readDocument(*kText, "test.graph", kSections);
    RAWFRAME_EXPECT(kRead.has_value());
    if (!kRead.has_value()) {
        return;
    }
    const auto kRewritten = writeDocument(*kRead);
    RAWFRAME_EXPECT(kRewritten.has_value() && *kRewritten == *kText && kRead->nodes.size() == 3 &&
                    kRead->sections.size() == 1 && kRead->presentation.size() == 1);
    RAWFRAME_EXPECT(kText->find("\"000000000000001f\"") == std::string::npos);
    // Another family's kind, or a section it does not declare, is refused.
    RAWFRAME_EXPECT(refusedWith(readDocument(*kText, "other.graph", kSections), GraphError::Invalid));
    RAWFRAME_EXPECT(refusedWith(readDocument(*kText, "test.graph"), GraphError::Invalid));
}

RAWFRAME_TEST(TheGrammarsRulesHold) {
    // A connection to no node, a cycle, a record out of form, a drawing of
    // no node, nodes out of order.
    Document dangling = sample();
    dangling.nodes[1].record = record("rawframe/surface@1", [] {
        Value inputs = Value::object();
        inputs.add("base_color", connectionValue({.node = 99, .output = "out"}));
        return inputs;
    }());
    RAWFRAME_EXPECT(refusedWith(validate(dangling), GraphError::Invalid));
    Document looped = sample();
    looped.nodes[0].record = record("rawframe/constant@1", [] {
        Value inputs = Value::object();
        inputs.add("back", connectionValue({.node = 0x2f00000000000002ULL, .output = "out"}));
        return inputs;
    }());
    RAWFRAME_EXPECT(refusedWith(validate(looped), GraphError::Invalid));
    Document malformed = sample();
    malformed.nodes[0].record = Value::object();
    malformed.nodes[0].record.add("params", Value::object());
    RAWFRAME_EXPECT(refusedWith(validate(malformed), GraphError::Invalid));
    Document stray = sample();
    stray.presentation.emplace_back(0x9f00000000000009ULL, Value::object());
    RAWFRAME_EXPECT(refusedWith(validate(stray), GraphError::Invalid));
    Document shuffled = sample();
    std::swap(shuffled.nodes[0], shuffled.nodes[1]);
    RAWFRAME_EXPECT(refusedWith(validate(shuffled), GraphError::Invalid));
    // Past a limit before the work.
    RAWFRAME_EXPECT(refusedWith(validate(sample(), {.maximumNodes = 2}), GraphError::OverLimit));
    RAWFRAME_EXPECT(
        refusedWith(readDocument(std::string(64, ' '), "test.graph", {}, {.maximumBytes = 8}), GraphError::OverLimit));
}

RAWFRAME_TEST(TheSemanticHashFollowsMeaningAlone) {
    const auto kHashOf = [](const Document& graph, NodeId surface) {
        return semanticHash(graph.nodes, {.kind = "test.graph", .outputs = {{"surface", surface}}});
    };
    const auto kHashed = kHashOf(sample(), 0x2f00000000000002ULL);
    RAWFRAME_EXPECT(kHashed.has_value());
    if (!kHashed.has_value()) {
        return;
    }
    const base::Sha256Digest kFirst = *kHashed;
    // Other ids, no drawings, and a node no output reaches change nothing.
    Document renamed = sample(0x5a00000000000005ULL, 0x0a0000000000000aULL);
    renamed.presentation.clear();
    std::erase_if(renamed.nodes, [](const Node& node) {
        return node.id == 0x3f00000000000003ULL;
    });
    const auto kRenamed = kHashOf(renamed, 0x0a0000000000000aULL);
    RAWFRAME_EXPECT(kRenamed.has_value() && *kRenamed == kFirst);
    // A literal changed changes it.
    Document changed = sample();
    changed.nodes[1].record = record("rawframe/surface@1", [] {
        Value inputs = Value::object();
        inputs.add("base_metalness", connectionValue({.node = 0x1f00000000000001ULL, .output = "out"}));
        inputs.add("specular_roughness", Value::real(0.5));
        return inputs;
    }());
    const auto kChanged = kHashOf(changed, 0x2f00000000000002ULL);
    RAWFRAME_EXPECT(kChanged.has_value() && *kChanged != kFirst);
    RAWFRAME_EXPECT(refusedWith(kHashOf(sample(), 0x7700000000000007ULL), GraphError::Invalid));
}
