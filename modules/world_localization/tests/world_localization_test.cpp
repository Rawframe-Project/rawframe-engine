// A client's text: keys of a table the game names, formatted in the locale
// the player asked for, or the one it falls back to.

#include "rawframe/localization/errors.h"
#include "rawframe/test/test.h"
#include "rawframe/world_localization/text.h"

#include <string>
#include <vector>

using namespace rawframe;
using namespace rawframe::localization;

namespace {

const base::Bits128 kHud{0, 0xa1};

world_localization::GameText game(std::string_view requested) {
    StringTable table{.sourceLocale = *parseLocale("en"), .entries = {}};
    table.entries["hud.score"] = SourceEntry{.message = "{$points} points", .description = {}};
    table.entries["menu.play"] = SourceEntry{.message = "Play", .description = {}};
    Translations turkish{.table = kHud, .locale = *parseLocale("tr"), .entries = {}};
    turkish.entries["menu.play"] = TranslatedEntry{.message = "Oyna", .sourceHash = sourceHashOf("Play")};
    const std::vector<TableDocument> kTables{{.id = kHud, .table = table}};
    const std::vector<Translations> kTranslations{turkish};
    return world_localization::GameText{
        *Catalog::build(kTables, kTranslations), {{"hud.strings", kHud}}, *intake(requested), *parseLocale("en")};
}

} // namespace

RAWFRAME_TEST(AKeyIsFormattedInTheAskedLocaleOrItsFallback) {
    const world_localization::GameText kTurkish = game("tr_TR");
    RAWFRAME_EXPECT(kTurkish.requested().text() == "tr-TR" && kTurkish.table("hud.strings") == kHud);
    RAWFRAME_EXPECT(kTurkish.format("hud.strings", "menu.play", {}) == "Oyna");
    // Turkish lacks the score: English serves it, in English numbers.
    const std::vector<Argument> kPoints{{"points", std::int64_t{1500}}};
    RAWFRAME_EXPECT(kTurkish.format("hud.strings", "hud.score", kPoints) == "1,500 points");
    RAWFRAME_EXPECT(game("de").format("hud.strings", "menu.play", {}) == "Play");
    // A table no text line names, and a key the table lacks.
    const auto kNoTable = kTurkish.format("other.strings", "menu.play", {});
    RAWFRAME_EXPECT(!kNoTable.has_value() && kNoTable.error().code() == code(LocalizationError::KeyUnknown));
    const auto kNoKey = kTurkish.format("hud.strings", "menu.quit", {});
    RAWFRAME_EXPECT(!kNoKey.has_value() && kNoKey.error().code() == code(LocalizationError::KeyUnknown));
}
