// An authored JSON document (D640): slots found by members and keyed items
// at any depth, set and unset with their objects' members in order, items
// made and removed whole at their places, emptied containers omitted but
// the root's own, a slot that does not hold what a delta leaves from
// refused, every batch one entry undone exactly, and nothing kept that the
// kind's reader refuses.

#include "rawframe/authoring/errors.h"
#include "rawframe/authoring/json_document.h"
#include "rawframe/test/test.h"

#include <array>
#include <string>

using namespace rawframe;
using namespace rawframe::authoring;
using document::Value;

namespace {

bool refusedWith(const auto& outcome, AuthoringError error) {
    return !outcome.has_value() && outcome.error().domain() == kAuthoringDomain &&
           outcome.error().code() == code(error);
}

constexpr std::array<std::string_view, 2> kRoot = {"kind", "nodes"};
constexpr std::array<std::string_view, 4> kNode = {"name", "size", "colour", "children"};

std::span<const std::string_view> orderOf(std::string_view holder) noexcept {
    return holder.empty() ? std::span<const std::string_view>{kRoot} : std::span<const std::string_view>{kNode};
}

/// Canonical JSON whose sizes are below a hundred.
result::Status reads(std::string_view text) {
    auto read = document::parseCanonical(text);
    if (!read.has_value()) {
        return std::unexpected<result::Error>{std::move(read).error()};
    }
    if (text.find("\"size\": 1000") != std::string_view::npos) {
        return result::fail(result::ErrorClass::InvalidArgument,
                            kAuthoringDomain,
                            code(AuthoringError::ValidationFailed),
                            "a size is below a hundred");
    }
    return {};
}

constexpr std::string_view kTree = R"({
  "kind": "tree",
  "nodes": [
    {
      "name": "hud",
      "children": [
        {
          "name": "points",
          "size": 14
        }
      ]
    }
  ]
}
)";

JsonPath nodePath(std::initializer_list<std::string_view> names) {
    JsonPath path;
    bool first = true;
    for (const std::string_view kName : names) {
        path.push_back(JsonStep{.member = first ? "nodes" : "children", .key = "name", .id = std::string{kName}});
        first = false;
    }
    return path;
}

JsonPath memberOf(JsonPath path, std::string_view member) {
    path.push_back(JsonStep{.member = std::string{member}, .key = {}, .id = {}});
    return path;
}

} // namespace

RAWFRAME_TEST(AJsonDocumentsSlotsChangeAndUndoExactly) {
    auto opened = JsonDocument::open(kTree, orderOf, reads);
    RAWFRAME_EXPECT(opened.has_value());
    if (!opened.has_value()) {
        return;
    }
    JsonDocument& tree = *opened;
    const JsonPath kPointsSize = memberOf(nodePath({"hud", "points"}), "size");
    RAWFRAME_EXPECT(slotAt(tree.root(), kPointsSize)->integer() == 14);
    RAWFRAME_EXPECT(placeAt(tree.root(), nodePath({"hud", "points"})) == 0);
    // A deep member changed, one set before it in order, and a child made
    // first among its siblings: one entry.
    auto staged = tree.stage(0);
    RAWFRAME_EXPECT(staged.has_value());
    RAWFRAME_EXPECT(
        tree.apply(*staged, JsonDelta{.path = kPointsSize, .before = Value::integer(14), .after = Value::integer(16)})
            .has_value());
    RAWFRAME_EXPECT(
        tree.apply(*staged,
                   JsonDelta{.path = memberOf(nodePath({"hud"}), "size"), .before = {}, .after = Value::integer(3)})
            .has_value());
    Value coin = Value::object();
    coin.add("name", Value::string("coin"));
    RAWFRAME_EXPECT(
        tree.apply(*staged, JsonDelta{.path = nodePath({"hud", "coin"}), .place = 0, .before = {}, .after = coin})
            .has_value());
    const auto kKept = tree.keep(std::move(*staged));
    RAWFRAME_EXPECT(kKept.has_value() && kKept->deltas == 3 && tree.generation() == 1);
    RAWFRAME_EXPECT(placeAt(tree.root(), nodePath({"hud", "points"})) == 1);
    const std::string kText = tree.text();
    RAWFRAME_EXPECT(kText.find("\"name\": \"hud\",\n      \"size\": 3,\n      \"children\"") != std::string::npos);
    // Both children removed: the emptied array is omitted; the root's own
    // nodes, emptied too, are kept.
    auto emptied = tree.stage(1);
    RAWFRAME_EXPECT(emptied.has_value());
    RAWFRAME_EXPECT(
        tree.apply(*emptied, JsonDelta{.path = nodePath({"hud", "coin"}), .place = 0, .before = coin, .after = {}})
            .has_value());
    RAWFRAME_EXPECT(tree.apply(*emptied,
                               JsonDelta{.path = nodePath({"hud", "points"}),
                                         .place = 0,
                                         .before = slotAt(emptied->root, nodePath({"hud", "points"})),
                                         .after = {}})
                        .has_value());
    RAWFRAME_EXPECT(slotAt(emptied->root, memberOf(nodePath({"hud"}), "children")) == std::nullopt);
    RAWFRAME_EXPECT(tree.apply(*emptied,
                               JsonDelta{.path = nodePath({"hud"}),
                                         .place = 0,
                                         .before = slotAt(emptied->root, nodePath({"hud"})),
                                         .after = {}})
                        .has_value());
    RAWFRAME_EXPECT(tree.keep(std::move(*emptied)).has_value());
    RAWFRAME_EXPECT(tree.text() == "{\n  \"kind\": \"tree\",\n  \"nodes\": []\n}\n");
    // Back exactly, and forward again.
    RAWFRAME_EXPECT(tree.undo(2).has_value() && tree.text() == kText);
    RAWFRAME_EXPECT(tree.undo(3).has_value() && tree.text() == kTree && !tree.dirty());
    RAWFRAME_EXPECT(tree.redo(4).has_value() && tree.text() == kText);
}

RAWFRAME_TEST(AJsonDocumentRefusesWhatDoesNotFit) {
    auto opened = JsonDocument::open(kTree, orderOf, reads);
    RAWFRAME_EXPECT(opened.has_value());
    if (!opened.has_value()) {
        return;
    }
    JsonDocument& tree = *opened;
    const JsonPath kPointsSize = memberOf(nodePath({"hud", "points"}), "size");
    RAWFRAME_EXPECT(refusedWith(tree.stage(1), AuthoringError::TargetStale));
    auto staged = tree.stage(0);
    RAWFRAME_EXPECT(staged.has_value());
    // A slot that does not hold what the delta leaves from, an item that is
    // not there to pass through, and a place past an array's.
    RAWFRAME_EXPECT(
        refusedWith(tree.apply(*staged, JsonDelta{.path = kPointsSize, .before = Value::integer(9), .after = {}}),
                    AuthoringError::DeltaMismatch));
    RAWFRAME_EXPECT(refusedWith(
        tree.apply(
            *staged,
            JsonDelta{.path = memberOf(nodePath({"hud", "gone"}), "size"), .before = {}, .after = Value::integer(1)}),
        AuthoringError::TargetNotFound));
    RAWFRAME_EXPECT(refusedWith(
        tree.apply(*staged,
                   JsonDelta{.path = nodePath({"hud", "late"}), .place = 5, .before = {}, .after = Value::object()}),
        AuthoringError::DeltaMismatch));
    // What the reader refuses is not kept.
    RAWFRAME_EXPECT(
        tree.apply(*staged, JsonDelta{.path = kPointsSize, .before = Value::integer(14), .after = Value::integer(1000)})
            .has_value());
    RAWFRAME_EXPECT(refusedWith(tree.keep(std::move(*staged)), AuthoringError::ValidationFailed));
    RAWFRAME_EXPECT(tree.text() == kTree && tree.generation() == 0 && !tree.canUndo());
}
