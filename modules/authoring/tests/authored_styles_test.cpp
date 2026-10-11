// A style sheet open for authoring (D636): a part set, changed, and unset,
// a variant made and gone with its first and last part, each change one
// entry that undo and redo walk exactly back to the bytes before, the
// clean place kept, a batch one entry or nothing, changes the sheet does
// not read refused with the document as it was, and the sheet read whole;
// classes made, removed, and renamed (D638); transitions set (D639).

#include "rawframe/authoring/authored_styles.h"
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

const base::Bits128 kDocument{9, 9};
constexpr std::uint64_t kButton = 0x85ecf7b643b63d65;

constexpr std::string_view kSheet = R"({
  "kind": "ui.styles",
  "formatVersion": 1,
  "styles": [
    {
      "styleId": "85ecf7b643b63d65",
      "name": "button",
      "base": {
        "fill": "#101820c0",
        "radius": 6
      },
      "hovered": {
        "fill": "#304860e0"
      },
      "transitions": [
        {
          "parts": [
            "fill"
          ],
          "seconds": 0.12,
          "easing": "ease_out"
        }
      ]
    },
    {
      "styleId": "1d2c3b4a59687706",
      "name": "panel"
    }
  ]
}
)";

/// One part set, alone in its batch.
result::Result<Committed> set(AuthoredStyles& styles,
                              std::uint64_t generation,
                              std::uint64_t style,
                              std::string_view variant,
                              std::string_view part,
                              std::optional<Value> value) {
    const std::array<Operation, 1> kOne = {
        SetStylePart{.style = style, .variant = std::string{variant}, .part = std::string{part}, .value = value}};
    return styles.change(generation, kOne);
}

std::string fillOf(const AuthoredStyles& styles, std::string_view variant) {
    const auto kFill = styles.part(kButton, variant, "fill");
    return kFill.has_value() && kFill->text() != nullptr ? *kFill->text() : "";
}

} // namespace

RAWFRAME_TEST(AStyleSheetsPartsChangeAndUndoExactly) {
    auto opened = AuthoredStyles::open(kDocument, kSheet);
    RAWFRAME_EXPECT(opened.has_value());
    if (!opened.has_value()) {
        return;
    }
    AuthoredStyles& styles = **opened;
    RAWFRAME_EXPECT(styles.text() == kSheet && !styles.dirty() && styles.generation() == 0);
    RAWFRAME_EXPECT(fillOf(styles, "base") == "#101820c0" && fillOf(styles, "pressed").empty());
    // A part changed: the sheet reads it, one entry.
    RAWFRAME_EXPECT(set(styles, 0, kButton, "base", "fill", Value::string("#c01818c0")).has_value());
    RAWFRAME_EXPECT(styles.generation() == 1 && styles.dirty() && styles.undoable() == 1);
    RAWFRAME_EXPECT(fillOf(styles, "base") == "#c01818c0" && styles.sheet().styles[0].looks[0].look.fill == 0xc01818c0);
    const std::string kChanged = styles.text();
    // A variant made by its first part, in its place among the class's
    // fields, and gone with its last.
    RAWFRAME_EXPECT(set(styles, 1, kButton, "pressed", "fill", Value::string("#080c14e0")).has_value());
    RAWFRAME_EXPECT(styles.text().find("\"hovered\"") < styles.text().find("\"pressed\"") &&
                    styles.text().find("\"pressed\"") < styles.text().find("\"transitions\""));
    RAWFRAME_EXPECT(set(styles, 2, kButton, "pressed", "fill", std::nullopt).has_value());
    RAWFRAME_EXPECT(styles.text() == kChanged && styles.generation() == 3);
    // The same value again is no change and no entry.
    const auto kSame = set(styles, 3, kButton, "base", "radius", Value::integer(6));
    RAWFRAME_EXPECT(kSame.has_value() && kSame->deltas == 0 && styles.undoable() == 3);
    // A class with no look yet takes one.
    RAWFRAME_EXPECT(set(styles, 3, 0x1d2c3b4a59687706, "base", "clip", Value::boolean(true)).has_value());
    RAWFRAME_EXPECT(styles.sheet().styles[1].looks[0].look.clip);
    // Back to the start exactly, the clean place found again, then forward.
    for (int back = 0; back < 4; ++back) {
        RAWFRAME_EXPECT(styles.undo(styles.generation()).has_value());
    }
    RAWFRAME_EXPECT(styles.text() == kSheet && !styles.dirty() && !styles.canUndo());
    RAWFRAME_EXPECT(styles.redo(styles.generation()).has_value() && styles.text() == kChanged);
    // A new change drops what could be redone.
    RAWFRAME_EXPECT(set(styles, styles.generation(), kButton, "hovered", "radius", Value::integer(8)).has_value());
    RAWFRAME_EXPECT(!styles.canRedo() && styles.undoable() == 2);
}

RAWFRAME_TEST(AStyleSheetsBatchIsOneEntryOrNothing) {
    auto opened = AuthoredStyles::open(kDocument, kSheet);
    RAWFRAME_EXPECT(opened.has_value());
    if (!opened.has_value()) {
        return;
    }
    AuthoredStyles& styles = **opened;
    // Two parts and a part set twice: one entry, the last value kept.
    const std::array<Operation, 3> kThree = {
        SetStylePart{.style = kButton, .variant = "base", .part = "fill", .value = Value::string("#000000ff")},
        SetStylePart{.style = kButton, .variant = "hovered", .part = "radius", .value = Value::integer(3)},
        SetStylePart{.style = kButton, .variant = "base", .part = "fill", .value = Value::string("#ffffffff")}};
    const auto kDone = styles.change(0, kThree);
    RAWFRAME_EXPECT(kDone.has_value() && kDone->deltas == 3 && styles.undoable() == 1);
    RAWFRAME_EXPECT(fillOf(styles, "base") == "#ffffffff" && styles.sheet().styles[0].looks[2].look.radius == 3);
    RAWFRAME_EXPECT(styles.undo(1).has_value() && styles.text() == kSheet);
    // One refused, by its index: nothing kept. A scene's operation too.
    const std::array<Operation, 2> kBad = {
        SetStylePart{.style = kButton, .variant = "base", .part = "fill", .value = Value::string("#000000ff")},
        SetStylePart{.style = kButton, .variant = "base", .part = "fill", .value = Value::string("black")}};
    const auto kRefused = styles.change(2, kBad);
    RAWFRAME_EXPECT(refusedWith(kRefused, AuthoringError::ValidationFailed) && styles.text() == kSheet);
    const std::array<Operation, 1> kScenes = {RestoreEntity{}};
    const auto kScene = styles.change(2, kScenes);
    RAWFRAME_EXPECT(refusedWith(kScene, AuthoringError::ValidationFailed));
    bool indexed = false;
    for (const auto& kField : kScene.error().context()) {
        indexed = indexed || (kField.key == "index" && kField.value == "0");
    }
    RAWFRAME_EXPECT(indexed);
}

RAWFRAME_TEST(AStyleSheetsClassesAreMadeRemovedAndRenamed) {
    auto opened = AuthoredStyles::open(kDocument, kSheet);
    RAWFRAME_EXPECT(opened.has_value());
    if (!opened.has_value()) {
        return;
    }
    AuthoredStyles& styles = **opened;
    constexpr std::uint64_t kTip = 0x00000000000000a1;
    // Made first among the classes, then given a look and a new name, in
    // one entry.
    const std::array<Operation, 3> kMade = {
        CreateStyleClass{.style = kTip, .name = "tip", .place = 0},
        SetStylePart{.style = kTip, .variant = "base", .part = "radius", .value = Value::integer(4)},
        RenameStyleClass{.style = kTip, .name = "tooltip"}};
    RAWFRAME_EXPECT(styles.change(0, kMade).has_value());
    RAWFRAME_EXPECT(styles.sheet().styles.size() == 3 && styles.sheet().styles[0].id == kTip &&
                    styles.sheet().styles[0].name == "tooltip" && styles.sheet().styles[0].looks[0].look.radius == 4);
    RAWFRAME_EXPECT(styles.text().find("\"styleId\": \"00000000000000a1\"") != std::string::npos);
    RAWFRAME_EXPECT(summaryOf(styles.journalAt(0)) == "create class tip and 2 more");
    // The button removed, its looks and transitions with it; undone, it is
    // back where it stood.
    const std::array<Operation, 1> kRemoved = {RemoveStyleClass{.style = kButton}};
    RAWFRAME_EXPECT(styles.change(1, kRemoved).has_value() && styles.sheet().styles.size() == 2 &&
                    !styles.sheet().styleWithId(kButton).has_value());
    RAWFRAME_EXPECT(summaryOf(styles.journalAt(1)) == "delete class button");
    RAWFRAME_EXPECT(styles.undo(2).has_value() && styles.sheet().styleWithId(kButton) == 1);
    RAWFRAME_EXPECT(styles.undo(3).has_value() && styles.text() == kSheet);
    // Refused: an identity the sheet holds or nought, a place past the
    // classes, an unknown class removed or renamed; nothing kept.
    const std::array<Operation, 1> kTaken = {CreateStyleClass{.style = kButton, .name = "again"}};
    const std::array<Operation, 1> kNought = {CreateStyleClass{.style = 0, .name = "none"}};
    const std::array<Operation, 1> kPast = {CreateStyleClass{.style = kTip, .name = "tip", .place = 3}};
    const std::array<Operation, 1> kGone = {RemoveStyleClass{.style = kTip}};
    const std::array<Operation, 1> kNamed = {RenameStyleClass{.style = kTip, .name = "x"}};
    RAWFRAME_EXPECT(refusedWith(styles.change(4, kTaken), AuthoringError::Conflict));
    RAWFRAME_EXPECT(refusedWith(styles.change(4, kNought), AuthoringError::Conflict));
    RAWFRAME_EXPECT(refusedWith(styles.change(4, kPast), AuthoringError::ValidationFailed));
    RAWFRAME_EXPECT(refusedWith(styles.change(4, kGone), AuthoringError::TargetNotFound));
    RAWFRAME_EXPECT(refusedWith(styles.change(4, kNamed), AuthoringError::TargetNotFound));
    // A name the sheet does not read: past its 64 characters.
    const std::array<Operation, 1> kLong = {RenameStyleClass{.style = kButton, .name = std::string(65, 'a')}};
    RAWFRAME_EXPECT(refusedWith(styles.change(4, kLong), AuthoringError::ValidationFailed));
    RAWFRAME_EXPECT(styles.text() == kSheet);
}

RAWFRAME_TEST(AStyleClassesTransitionsAreSetWhole) {
    auto opened = AuthoredStyles::open(kDocument, kSheet);
    RAWFRAME_EXPECT(opened.has_value());
    if (!opened.has_value()) {
        return;
    }
    AuthoredStyles& styles = **opened;
    // A spring in place of the button's timed fill; the panel given its
    // first; the button's taken away; each undone exactly.
    const auto kSpring = document::parse(R"([{"parts": ["fill", "radius"], "kind": "spring", "frequency": 4,
        "dampingRatio": 0.8}])");
    const std::array<Operation, 1> kSprung = {SetStyleTransitions{.style = kButton, .value = *kSpring}};
    RAWFRAME_EXPECT(styles.change(0, kSprung).has_value());
    RAWFRAME_EXPECT(styles.sheet().styles[0].transitions.size() == 1 &&
                    styles.sheet().styles[0].transitions[0].second.kind == ui::Transition::Kind::Spring);
    RAWFRAME_EXPECT(summaryOf(styles.journalAt(0)) == "set transitions");
    const std::array<Operation, 2> kMoved = {SetStyleTransitions{.style = 0x1d2c3b4a59687706, .value = *kSpring},
                                             SetStyleTransitions{.style = kButton, .value = std::nullopt}};
    RAWFRAME_EXPECT(styles.change(1, kMoved).has_value() && styles.sheet().styles[0].transitions.empty() &&
                    styles.sheet().styles[1].transitions.size() == 1);
    RAWFRAME_EXPECT(styles.undo(2).has_value() && styles.undo(3).has_value() && styles.text() == kSheet);
    // Refused: what the reader does not read, as a transition of no part
    // or a list that is no list.
    const std::array<Operation, 1> kNoPart = {
        SetStyleTransitions{.style = kButton, .value = *document::parse(R"([{"parts": [], "seconds": 1}])")}};
    const std::array<Operation, 1> kNoList = {SetStyleTransitions{.style = kButton, .value = Value::integer(1)}};
    RAWFRAME_EXPECT(refusedWith(styles.change(4, kNoPart), AuthoringError::ValidationFailed));
    RAWFRAME_EXPECT(refusedWith(styles.change(4, kNoList), AuthoringError::ValidationFailed));
    RAWFRAME_EXPECT(styles.text() == kSheet);
}

RAWFRAME_TEST(AStyleSheetRefusesWhatItDoesNotRead) {
    auto opened = AuthoredStyles::open(kDocument, kSheet);
    RAWFRAME_EXPECT(opened.has_value());
    if (!opened.has_value()) {
        return;
    }
    AuthoredStyles& styles = **opened;
    // Another generation, another class, a part or variant a look has not.
    RAWFRAME_EXPECT(refusedWith(set(styles, 4, kButton, "base", "fill", std::nullopt), AuthoringError::TargetStale));
    RAWFRAME_EXPECT(refusedWith(set(styles, 0, 1, "base", "fill", std::nullopt), AuthoringError::TargetNotFound));
    RAWFRAME_EXPECT(
        refusedWith(set(styles, 0, kButton, "base", "colour", std::nullopt), AuthoringError::ValidationFailed));
    RAWFRAME_EXPECT(
        refusedWith(set(styles, 0, kButton, "dragged", "fill", std::nullopt), AuthoringError::ValidationFailed));
    // Values the sheet does not read: a color without its alpha, a radius
    // past a screen, a shadow without its color.
    RAWFRAME_EXPECT(refusedWith(set(styles, 0, kButton, "base", "fill", Value::string("#c01818")),
                                AuthoringError::ValidationFailed));
    RAWFRAME_EXPECT(
        refusedWith(set(styles, 0, kButton, "base", "radius", Value::integer(5000)), AuthoringError::ValidationFailed));
    RAWFRAME_EXPECT(refusedWith(set(styles, 0, kButton, "focused", "outerShadow", Value::object()),
                                AuthoringError::ValidationFailed));
    RAWFRAME_EXPECT(styles.text() == kSheet && styles.generation() == 0 && !styles.canUndo());
    // Nothing undone.
    RAWFRAME_EXPECT(refusedWith(styles.undo(0), AuthoringError::TargetNotFound));
    // Read whole by its query, as a query document names it; a scene's
    // query refused.
    const auto kQueries = readQueries(R"({"formatVersion": 1, "kind": "authoring.query",
        "queries": [{"operation": "styles.read_sheet"}]})");
    RAWFRAME_EXPECT(kQueries.has_value() && kQueries->size() == 1);
    const auto kRead = answer(styles, kQueries.has_value() ? kQueries->front() : Query{ListEntities{}});
    RAWFRAME_EXPECT(kRead.has_value() && document::write(*kRead->find("sheet")) == kSheet);
    RAWFRAME_EXPECT(refusedWith(answer(styles, Query{ListEntities{}}), AuthoringError::ValidationFailed));
    // A text that is no sheet does not open.
    RAWFRAME_EXPECT(refusedWith(AuthoredStyles::open(kDocument, "{}"), AuthoringError::ValidationFailed));
}
