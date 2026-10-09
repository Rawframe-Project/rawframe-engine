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
    RAWFRAME_EXPECT(kTurkish.requested(0).text() == "tr-TR" && kTurkish.table("hud.strings") == kHud);
    RAWFRAME_EXPECT(kTurkish.format(0, "hud.strings", "menu.play", {}) == "Oyna");
    // Turkish lacks the score: English serves it, in English numbers.
    const std::vector<Argument> kPoints{{"points", std::int64_t{1500}}};
    RAWFRAME_EXPECT(kTurkish.format(0, "hud.strings", "hud.score", kPoints) == "1,500 points");
    RAWFRAME_EXPECT(game("de").format(0, "hud.strings", "menu.play", {}) == "Play");
    // A table no text line names, and a key the table lacks.
    const auto kNoTable = kTurkish.format(0, "other.strings", "menu.play", {});
    RAWFRAME_EXPECT(!kNoTable.has_value() && kNoTable.error().code() == code(LocalizationError::KeyUnknown));
    const auto kNoKey = kTurkish.format(0, "hud.strings", "menu.quit", {});
    RAWFRAME_EXPECT(!kNoKey.has_value() && kNoKey.error().code() == code(LocalizationError::KeyUnknown));
}

RAWFRAME_TEST(APlayerChoosesAmongTheOfferedLocalesWhilePlaying) {
    // D539: the default first, then the translations' locales; choosing one
    // moves the revision once, and what is formatted follows.
    StringTable table{.sourceLocale = *parseLocale("en"), .entries = {}};
    table.entries["menu.play"] = SourceEntry{.message = "Play", .description = {}};
    Translations turkish{.table = kHud, .locale = *parseLocale("tr"), .entries = {}};
    turkish.entries["menu.play"] = TranslatedEntry{.message = "Oyna", .sourceHash = sourceHashOf("Play")};
    const std::vector<TableDocument> kTables{{.id = kHud, .table = table}};
    const std::vector<Translations> kTranslations{turkish};
    world_localization::GameText text{*Catalog::build(kTables, kTranslations),
                                      {{"hud.strings", kHud}},
                                      *parseLocale("de"),
                                      *parseLocale("en"),
                                      {*parseLocale("en"), *parseLocale("tr")}};
    // A configured locale none of them is chosen past the last.
    RAWFRAME_EXPECT(text.offered().size() == 2 && text.chosen(0) == 2 && text.revision() == 0);
    RAWFRAME_EXPECT(text.choose(0, 1) && text.chosen(0) == 1 && text.revision() == 1);
    RAWFRAME_EXPECT(text.format(0, "hud.strings", "menu.play", {}) == "Oyna");
    // The one asked for again changes nothing; one past them is refused.
    RAWFRAME_EXPECT(text.choose(0, 1) && text.revision() == 1);
    RAWFRAME_EXPECT(!text.choose(0, 2) && text.chosen(0) == 1);
    RAWFRAME_EXPECT(text.choose(0, 0) && text.revision() == 2);
    RAWFRAME_EXPECT(text.format(0, "hud.strings", "menu.play", {}) == "Play");
    // Each local player has their own (ADR-0050): the second choosing
    // Turkish leaves the first in English, and a third keeps the one
    // configured.
    RAWFRAME_EXPECT(text.choose(1, 1) && text.revision() == 3 && text.chosen(1) == 1 && text.chosen(0) == 0);
    RAWFRAME_EXPECT(text.format(1, "hud.strings", "menu.play", {}) == "Oyna");
    RAWFRAME_EXPECT(text.format(0, "hud.strings", "menu.play", {}) == "Play");
    RAWFRAME_EXPECT(text.chosen(2) == 2 && text.requested(2).text() == "de");
}

RAWFRAME_TEST(DisplayKeysModeShowsEveryKeyAsItsToken) {
    // SPEC-0033's development-only display-keys mode (D540): the key itself,
    // whatever the locale, the table, or the key's presence.
    world_localization::GameText text = game("tr");
    RAWFRAME_EXPECT(text.format(0, "hud.strings", "menu.play", {}) == "Oyna");
    text.showKeys();
    RAWFRAME_EXPECT(text.format(0, "hud.strings", "menu.play", {}) == "menu.play");
    RAWFRAME_EXPECT(text.format(0, "other.strings", "menu.quit", {}) == "menu.quit");
}
