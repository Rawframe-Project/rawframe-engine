// A UI document open for authoring (D641): a node's field set and unset
// wherever the node stands, nodes made inside others and removed with
// what is inside them, each batch one entry undone exactly, what the
// document's reader refuses refused with the document as it was, and the
// document read whole.

#include "rawframe/authoring/authored_ui.h"
#include "rawframe/authoring/errors.h"
#include "rawframe/authoring/request.h"
#include "rawframe/test/test.h"

#include <array>
#include <memory>
#include <string>

using namespace rawframe;
using namespace rawframe::authoring;
using document::Value;

namespace {

bool refusedWith(const auto& outcome, AuthoringError error) {
    return !outcome.has_value() && outcome.error().domain() == kAuthoringDomain &&
           outcome.error().code() == code(error);
}

const base::Bits128 kDocument{4, 4};

constexpr std::string_view kHud = R"({
  "kind": "ui.document",
  "formatVersion": 1,
  "nodes": [
    {
      "component": "game.hud",
      "widthOffset": 240,
      "children": [
        {
          "component": "game.points",
          "textSize": 14
        }
      ]
    }
  ]
}
)";

} // namespace

RAWFRAME_TEST(AUiDocumentsNodesChangeAndUndoExactly) {
    auto opened = AuthoredUi::open(kDocument, kHud);
    RAWFRAME_EXPECT(opened.has_value());
    if (!opened.has_value()) {
        return;
    }
    AuthoredUi& hud = **opened;
    RAWFRAME_EXPECT(hud.read().nodes.size() == 2 && hud.pathOf("game.points")->size() == 2);
    // A child's field set, a root's unset, and a node made first inside
    // the hud with a field of its own: one entry.
    const std::array<Operation, 4> kChanged = {
        SetUiField{.node = "game.points", .field = "fill", .value = Value::string("#101820c0")},
        SetUiField{.node = "game.hud", .field = "widthOffset", .value = std::nullopt},
        CreateUiNode{.node = "game.coin", .parent = "game.hud", .place = 0},
        SetUiField{.node = "game.coin", .field = "textSize", .value = Value::integer(12)}};
    const auto kDone = hud.change(0, kChanged);
    RAWFRAME_EXPECT(kDone.has_value() && kDone->deltas == 4 && hud.undoable() == 1);
    RAWFRAME_EXPECT(hud.read().nodes.size() == 3 && hud.read().nodes[1].component == "game.coin" &&
                    hud.read().nodes[0].fields.widthOffset == 0 && hud.read().nodes[2].fields.fill == 0x101820c0);
    RAWFRAME_EXPECT(hud.summaryAt(0) == "set fill of game.points and 3 more");
    const std::string kChangedText = hud.text();
    // The hud removed with both inside it; a root made after; undone.
    const std::array<Operation, 2> kRemoved = {RemoveUiNode{.node = "game.hud"},
                                               CreateUiNode{.node = "game.menu", .parent = std::nullopt}};
    RAWFRAME_EXPECT(hud.change(1, kRemoved).has_value() && hud.read().nodes.size() == 1 &&
                    hud.read().nodes[0].component == "game.menu");
    RAWFRAME_EXPECT(hud.summaryAt(1) == "delete node game.hud and 1 more");
    RAWFRAME_EXPECT(hud.undo(2).has_value() && hud.text() == kChangedText);
    RAWFRAME_EXPECT(hud.undo(3).has_value() && hud.text() == kHud && !hud.dirty());
    // Read whole by its query; another kind's refused.
    const auto kRead = answer(hud, Query{ReadUiDocument{}});
    RAWFRAME_EXPECT(kRead.has_value() && document::write(*kRead->find("document")) == kHud);
    RAWFRAME_EXPECT(refusedWith(answer(hud, Query{ReadSheet{}}), AuthoringError::ValidationFailed));
}

RAWFRAME_TEST(AUiDocumentRefusesWhatItDoesNotRead) {
    auto opened = AuthoredUi::open(kDocument, kHud);
    RAWFRAME_EXPECT(opened.has_value());
    if (!opened.has_value()) {
        return;
    }
    AuthoredUi& hud = **opened;
    const auto kOne = [&hud](Operation operation) {
        const std::array<Operation, 1> kBatch = {std::move(operation)};
        return hud.change(0, kBatch);
    };
    RAWFRAME_EXPECT(refusedWith(kOne(SetUiField{.node = "game.gone", .field = "fill", .value = std::nullopt}),
                                AuthoringError::TargetNotFound));
    RAWFRAME_EXPECT(refusedWith(kOne(SetUiField{.node = "game.hud", .field = "children", .value = std::nullopt}),
                                AuthoringError::ValidationFailed));
    RAWFRAME_EXPECT(refusedWith(kOne(SetUiField{.node = "game.hud", .field = "colour", .value = std::nullopt}),
                                AuthoringError::ValidationFailed));
    RAWFRAME_EXPECT(refusedWith(kOne(SetUiField{.node = "game.hud", .field = "fill", .value = Value::string("red")}),
                                AuthoringError::ValidationFailed));
    RAWFRAME_EXPECT(
        refusedWith(kOne(CreateUiNode{.node = "game.points", .parent = std::nullopt}), AuthoringError::Conflict));
    RAWFRAME_EXPECT(
        refusedWith(kOne(CreateUiNode{.node = "game.coin", .parent = "game.gone"}), AuthoringError::TargetNotFound));
    RAWFRAME_EXPECT(refusedWith(kOne(CreateUiNode{.node = "game.coin", .parent = "game.hud", .place = 2}),
                                AuthoringError::ValidationFailed));
    RAWFRAME_EXPECT(refusedWith(kOne(RemoveUiNode{.node = "game.gone"}), AuthoringError::TargetNotFound));
    RAWFRAME_EXPECT(refusedWith(kOne(RemoveStyleClass{.style = 1}), AuthoringError::ValidationFailed));
    RAWFRAME_EXPECT(hud.text() == kHud && hud.generation() == 0 && !hud.canUndo());
    // A query document names it.
    const auto kQueries = readQueries(R"({"formatVersion": 1, "kind": "authoring.query",
        "queries": [{"operation": "ui.read_document"}]})");
    RAWFRAME_EXPECT(kQueries.has_value() && std::holds_alternative<ReadUiDocument>(kQueries->front()));
    RAWFRAME_EXPECT(refusedWith(AuthoredUi::open(kDocument, "{}"), AuthoringError::ValidationFailed));
}
