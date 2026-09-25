// Catalogs (SPEC-0033): tables and translations checked together, stale
// entries reported, and keys resolved along the fallback chain and
// formatted in the locale that served them.

#include "rawframe/localization/catalog.h"
#include "rawframe/localization/errors.h"
#include "rawframe/test/test.h"

#include <string>
#include <string_view>
#include <vector>

using namespace rawframe;
using namespace rawframe::localization;

namespace {

bool refusedWith(const auto& outcome, LocalizationError error) {
    return !outcome.has_value() && outcome.error().domain() == kLocalizationDomain &&
           outcome.error().code() == code(error);
}

const base::Bits128 kHud = base::parseBits128Hex("0123456789abcdef0123456789abcdef").value;

Locale tag(std::string_view text) {
    return *parseLocale(text);
}

constexpr std::string_view kFiles = ".input {$count :integer}\n.match $count\none {{One file}}\n* {{{$count} files}}";

TableDocument hud() {
    StringTable table{.sourceLocale = tag("en"), .entries = {}};
    table.entries["menu.play"] = SourceEntry{.message = "Play", .description = {}};
    table.entries["hud.files"] = SourceEntry{.message = std::string{kFiles}, .description = {}};
    table.entries["hud.score"] = SourceEntry{.message = "{$points} points", .description = {}};
    return TableDocument{.id = kHud, .table = std::move(table)};
}

Translations into(std::string_view locale, std::initializer_list<std::pair<std::string, std::string>> entries) {
    const TableDocument kSource = hud();
    Translations made{.table = kHud, .locale = tag(locale), .entries = {}};
    for (const auto& [key, message] : entries) {
        const auto kFound = kSource.table.entries.find(key);
        made.entries[key] = TranslatedEntry{
            .message = message,
            .sourceHash = sourceHashOf(kFound != kSource.table.entries.end() ? kFound->second.message : "")};
    }
    return made;
}

const std::vector<Translations>& translated() {
    static const std::vector<Translations> kMade{
        into("tr",
             {{"menu.play", "Oyna"},
              {"hud.files", ".input {$count :integer}\n.match $count\none {{Bir dosya}}\n* {{{$count} dosya}}"}}),
        into("de", {{"menu.play", "Spielen"}, {"hud.score", "{$points} Punkte"}}),
    };
    return kMade;
}

} // namespace

RAWFRAME_TEST(AKeyIsServedByTheFirstLocaleOfTheChainThatHasIt) {
    const std::vector<TableDocument> kTables{hud()};
    const auto kCatalog = Catalog::build(kTables, translated());
    RAWFRAME_EXPECT(kCatalog.has_value() && kCatalog->stale().empty());
    // A Turkish player in Turkey: `tr` has the key.
    RAWFRAME_EXPECT(kCatalog->resolve(kHud, "menu.play", tag("tr-TR"), tag("de")) == tag("tr"));
    RAWFRAME_EXPECT(kCatalog->format(kHud, "menu.play", tag("tr-TR"), tag("de"), {}) == "Oyna");
    // `tr` lacks the score: the project's default, German, serves it, in
    // German numbers.
    const std::vector<Argument> kScore{{"points", std::int64_t{1234}}};
    RAWFRAME_EXPECT(kCatalog->format(kHud, "hud.score", tag("tr"), tag("de"), kScore) == "1.234 Punkte");
    // With English the default, the source serves it, in English numbers.
    RAWFRAME_EXPECT(kCatalog->format(kHud, "hud.score", tag("tr"), tag("en"), kScore) == "1,234 points");
    // The Turkish plural in Turkish numbers.
    const std::vector<Argument> kOne{{"count", std::int64_t{1}}};
    const std::vector<Argument> kMany{{"count", std::int64_t{1234}}};
    RAWFRAME_EXPECT(kCatalog->format(kHud, "hud.files", tag("tr"), tag("en"), kOne) == "Bir dosya");
    RAWFRAME_EXPECT(kCatalog->format(kHud, "hud.files", tag("tr"), tag("en"), kMany) == "1.234 dosya");
    // A locale no one translated into falls to the default, then the source.
    RAWFRAME_EXPECT(kCatalog->resolve(kHud, "menu.play", tag("ja"), tag("de")) == tag("de"));
    RAWFRAME_EXPECT(kCatalog->resolve(kHud, "hud.files", tag("ja"), tag("de")) == tag("en"));
}

RAWFRAME_TEST(UnknownKeysAndBadArgumentsAreTheCallersToHear) {
    const std::vector<TableDocument> kTables{hud()};
    const auto kCatalog = Catalog::build(kTables, translated());
    RAWFRAME_EXPECT(
        refusedWith(kCatalog->format(kHud, "menu.quit", tag("en"), tag("en"), {}), LocalizationError::KeyUnknown));
    RAWFRAME_EXPECT(refusedWith(kCatalog->resolve(base::Bits128{1, 2}, "menu.play", tag("en"), tag("en")),
                                LocalizationError::KeyUnknown));
    RAWFRAME_EXPECT(
        refusedWith(kCatalog->format(kHud, "hud.score", tag("de"), tag("en"), {}), LocalizationError::ArgumentMissing));
}

RAWFRAME_TEST(AChangedSourceMakesItsTranslationsStale) {
    std::vector<TableDocument> tables{hud()};
    tables[0].table.entries["menu.play"].message = "Start";
    const auto kCatalog = Catalog::build(tables, translated());
    RAWFRAME_EXPECT(kCatalog.has_value());
    // Stale is a report, not a failure: the old words still serve.
    RAWFRAME_EXPECT(kCatalog->stale().size() == 2 && kCatalog->stale()[0].locale == tag("de") &&
                    kCatalog->stale()[1].locale == tag("tr") && kCatalog->stale()[1].key == "menu.play");
    RAWFRAME_EXPECT(kCatalog->format(kHud, "menu.play", tag("tr"), tag("en"), {}) == "Oyna");
}

RAWFRAME_TEST(DocumentsThatDoNotFitTogetherAreRefused) {
    const std::vector<TableDocument> kTables{hud()};
    const auto kBuilt = [&kTables](std::vector<Translations> translations) {
        return Catalog::build(kTables, translations);
    };
    // A key the table does not have, named in the refusal.
    const auto kOrphan = kBuilt({into("tr", {{"menu.quit", "Çık"}})});
    RAWFRAME_EXPECT(refusedWith(kOrphan, LocalizationError::Orphaned));
    bool named = false;
    for (const auto& field : kOrphan.error().context()) {
        named = named || (field.key == "key" && field.value == "menu.quit");
    }
    RAWFRAME_EXPECT(named);
    // An argument the source does not read; fewer is fine.
    RAWFRAME_EXPECT(refusedWith(kBuilt({into("tr", {{"hud.score", "{$points} puan, {$bonus} bonus"}})}),
                                LocalizationError::CatalogInvalid));
    RAWFRAME_EXPECT(kBuilt({into("tr", {{"hud.score", "Puanlar"}})}).has_value());
    // Two translations into one locale; one into the source locale.
    RAWFRAME_EXPECT(refusedWith(kBuilt({into("tr", {{"menu.play", "Oyna"}}), into("tr", {{"menu.play", "Başla"}})}),
                                LocalizationError::CatalogInvalid));
    RAWFRAME_EXPECT(refusedWith(kBuilt({into("en", {{"menu.play", "Go"}})}), LocalizationError::CatalogInvalid));
    // A table not in the catalog; a table given twice.
    Translations elsewhere = into("tr", {{"menu.play", "Oyna"}});
    elsewhere.table = base::Bits128{1, 2};
    RAWFRAME_EXPECT(refusedWith(kBuilt({elsewhere}), LocalizationError::CatalogInvalid));
    const std::vector<TableDocument> kTwice{hud(), hud()};
    RAWFRAME_EXPECT(refusedWith(Catalog::build(kTwice, {}), LocalizationError::CatalogInvalid));
    // A document out of its form, as reading it would refuse.
    RAWFRAME_EXPECT(refusedWith(kBuilt({into("tr", {{"menu.play", "{$x"}})}), LocalizationError::MessageInvalid));
    // Past the limits.
    RAWFRAME_EXPECT(
        refusedWith(Catalog::build(kTables, translated(), {.maximumLocales = 2}), LocalizationError::OverLimit));
    RAWFRAME_EXPECT(refusedWith(Catalog::build(kTwice, {}, {.maximumTables = 1}), LocalizationError::OverLimit));
}
