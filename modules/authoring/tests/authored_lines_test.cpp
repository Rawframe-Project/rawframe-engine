// A line document authored (D817): a line set, put in, and taken away by
// its place, the lines no change names (a comment among them) kept as they
// stand, a change its reader refuses kept nowhere, stale and missing places
// refused, and the history stepped back to the very text and forward.

#include "rawframe/authoring/authored_lines.h"
#include "rawframe/authoring/errors.h"
#include "rawframe/test/test.h"

#include <array>
#include <string>

using namespace rawframe;
using namespace rawframe::authoring;

namespace {

constexpr std::string_view kText = "# a game\nprogram game.kest\n\naspect 16 9\n";

/// Reads as its kind: no line but a comment or a blank begins with a
/// space, and every one names a program, an aspect, or a scene.
result::Status reads(std::string_view text) {
    std::size_t at = 0;
    while (at < text.size()) {
        const std::size_t kEnd = std::min(text.find('\n', at), text.size());
        const std::string_view kLine = text.substr(at, kEnd - at);
        const bool kSound = kLine.empty() || kLine.starts_with('#') || kLine.starts_with("program ") ||
                            kLine.starts_with("aspect ") || kLine.starts_with("scene ");
        if (!kSound) {
            return result::fail(result::ErrorClass::InvalidArgument,
                                kAuthoringDomain,
                                code(AuthoringError::ValidationFailed),
                                "a line names a program, an aspect, or a scene");
        }
        at = kEnd + 1;
    }
    return {};
}

result::ErrorCode codeOf(const result::Error& error) {
    return error.code();
}

} // namespace

RAWFRAME_TEST(ALineDocumentsLinesAreSetPutInAndTakenAway) {
    auto opened = AuthoredLines::open({}, kText, &reads);
    RAWFRAME_EXPECT(opened.has_value());
    AuthoredLines& lines = **opened;
    RAWFRAME_EXPECT(lines.text() == kText);
    const std::array<Operation, 3> kChanges = {
        Operation{SetLine{.line = 3, .text = std::string{"aspect 4 3"}}},
        Operation{InsertLine{.line = 4, .text = "scene land.scene"}},
        Operation{SetLine{.line = 2, .text = std::nullopt}},
    };
    const auto kChanged = lines.change(lines.generation(), kChanges);
    RAWFRAME_EXPECT(kChanged.has_value() && kChanged->deltas == 3);
    RAWFRAME_EXPECT(lines.text() == "# a game\nprogram game.kest\naspect 4 3\nscene land.scene\n");
    RAWFRAME_EXPECT(lines.summaryAt(0) == "set line 3 and 2 more");
    RAWFRAME_EXPECT(lines.deltasAt(0) == 3);
    RAWFRAME_EXPECT(lines.dirty());

    // A line set to what it holds changes nothing.
    const std::array<Operation, 1> kSame = {Operation{SetLine{.line = 1, .text = std::string{"program game.kest"}}}};
    const auto kNothing = lines.change(lines.generation(), kSame);
    RAWFRAME_EXPECT(kNothing.has_value() && kNothing->deltas == 0 && lines.undoable() == 1);

    RAWFRAME_EXPECT(lines.undo(lines.generation()).has_value());
    RAWFRAME_EXPECT(lines.text() == kText);
    RAWFRAME_EXPECT(!lines.dirty());
    RAWFRAME_EXPECT(lines.redo(lines.generation()).has_value());
    RAWFRAME_EXPECT(lines.text() == "# a game\nprogram game.kest\naspect 4 3\nscene land.scene\n");
}

RAWFRAME_TEST(ALineDocumentRefusesWhatItCannotKeep) {
    auto opened = AuthoredLines::open({}, kText, &reads);
    RAWFRAME_EXPECT(opened.has_value());
    AuthoredLines& lines = **opened;
    const std::uint64_t kAt = lines.generation();
    // Its reader's refusal, a place past the lines, a line end in a line,
    // another kind's operation, and a stale generation: nothing kept.
    const std::array<Operation, 1> kUnread = {Operation{SetLine{.line = 3, .text = std::string{" players 2"}}}};
    const auto kRefused = lines.change(kAt, kUnread);
    RAWFRAME_EXPECT(!kRefused.has_value() && codeOf(kRefused.error()) == code(AuthoringError::ValidationFailed));
    const std::array<Operation, 1> kPast = {Operation{SetLine{.line = 4, .text = std::string{"aspect 1 1"}}}};
    const auto kMissing = lines.change(kAt, kPast);
    RAWFRAME_EXPECT(!kMissing.has_value() && codeOf(kMissing.error()) == code(AuthoringError::TargetNotFound));
    const std::array<Operation, 1> kLast = {Operation{InsertLine{.line = 4, .text = "scene a.scene"}}};
    const std::array<Operation, 1> kBroken = {Operation{InsertLine{.line = 0, .text = "# one\n# two"}}};
    const auto kEnded = lines.change(kAt, kBroken);
    RAWFRAME_EXPECT(!kEnded.has_value() && codeOf(kEnded.error()) == code(AuthoringError::ValidationFailed));
    const std::array<Operation, 1> kOther = {Operation{SetDataValue{.path = "a", .value = std::nullopt}}};
    RAWFRAME_EXPECT(!lines.change(kAt, kOther).has_value());
    RAWFRAME_EXPECT(lines.text() == kText && lines.generation() == kAt && lines.undoable() == 0);
    RAWFRAME_EXPECT(!lines.undo(kAt).has_value());

    // One past the last line puts one in last; then the old generation is
    // stale.
    RAWFRAME_EXPECT(lines.change(kAt, kLast).has_value());
    RAWFRAME_EXPECT(lines.text() == std::string{kText} + "scene a.scene\n");
    const auto kStale = lines.change(kAt, kLast);
    RAWFRAME_EXPECT(!kStale.has_value() && codeOf(kStale.error()) == code(AuthoringError::TargetStale));

    // Unread at open.
    RAWFRAME_EXPECT(!AuthoredLines::open({}, "players 2\n", &reads).has_value());
}

RAWFRAME_TEST(ALineDocumentIsReadByItsLines) {
    auto opened = AuthoredLines::open({}, kText, &reads);
    RAWFRAME_EXPECT(opened.has_value());
    const auto kRead = answer(**opened, Query{ReadLines{}});
    RAWFRAME_EXPECT(kRead.has_value());
    const document::Value* kLines = kRead->find("lines");
    RAWFRAME_EXPECT(kLines != nullptr && kLines->items().size() == 4 && *kLines->items()[3].text() == "aspect 16 9");
    RAWFRAME_EXPECT(!answer(**opened, Query{ReadData{}}).has_value());
}
