// Pseudo-localization (SPEC-0033): text changed as asked, and nothing else
// of a message changed at all.

#include "rawframe/localization/catalog.h"
#include "rawframe/localization/errors.h"
#include "rawframe/localization/pseudo.h"
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

constexpr std::string_view kFiles = ".input {$count :integer}\n.match $count\n0 {{No files}}\none {{One file}}\n"
                                    "* {{{$count} files \\{ok\\}}}";

} // namespace

RAWFRAME_TEST(TextIsAccentedPaddedAndMarked) {
    RAWFRAME_EXPECT(pseudoLocalize("Play") == "[Ṗĺáý~~~~]");
    RAWFRAME_EXPECT(pseudoLocalize("Play", {.accents = false}) == "[Play~~~~]");
    RAWFRAME_EXPECT(pseudoLocalize("Play", {.padding = false, .markers = false}) == "Ṗĺáý");
    RAWFRAME_EXPECT(pseudoLocalize("Play", {.expansion = 1.5}) == "[Ṗĺáý~~]");
    // Placeholders stay; only text counts toward padding.
    RAWFRAME_EXPECT(pseudoLocalize("Hi, {$name :string}!", {.accents = false}) == "[Hi, {$name :string}!~~~~~]");
    RAWFRAME_EXPECT(pseudoLocalize("Go", {.accents = false, .padding = false, .markers = false, .mirror = true}) ==
                    "\xE2\x80\xAEGo\xE2\x80\xAC");
    RAWFRAME_EXPECT(pseudoLocalize("", {.accents = false}) == "[]");
}

RAWFRAME_TEST(ExpansionFollowsIbmsRatios) {
    RAWFRAME_EXPECT(expansionFor(1) == 2.0 && expansionFor(10) == 2.0 && expansionFor(11) == 1.8);
    RAWFRAME_EXPECT(expansionFor(30) == 1.6 && expansionFor(50) == 1.4 && expansionFor(51) == 1.3 &&
                    expansionFor(500) == 1.3);
    // Twenty characters to 36: sixteen more.
    const auto kMade = pseudoLocalize(std::string(20, 'x'), {.accents = false, .markers = false});
    RAWFRAME_EXPECT(kMade == std::string(20, 'x') + std::string(16, '~'));
    RAWFRAME_EXPECT(refusedWith(pseudoLocalize("x", {.expansion = 3.5}), LocalizationError::OverLimit));
    RAWFRAME_EXPECT(refusedWith(pseudoLocalize("x", {.expansion = 0.5}), LocalizationError::OverLimit));
}

RAWFRAME_TEST(OnlyTextChanges) {
    const auto kMade = pseudoLocalize(kFiles);
    RAWFRAME_EXPECT(kMade == ".input {$count :integer}\n.match $count\n0 {{[Ñó ƒíĺéš~~~~~~~~]}}\n"
                             "one {{[Óñé ƒíĺé~~~~~~~~]}}\n* {{[{$count} ƒíĺéš \\{óķ\\}~~~~~~~~~]}}");
    // The same declarations, selectors, keys, and placeholders.
    const auto kSource = parseMessage(kFiles);
    const auto kPseudo = parseMessage(*kMade);
    RAWFRAME_EXPECT(kSource.has_value() && kPseudo.has_value());
    RAWFRAME_EXPECT(kPseudo->declarations == kSource->declarations && kPseudo->selectors == kSource->selectors &&
                    argumentsOf(*kPseudo) == argumentsOf(*kSource));
    for (std::size_t at = 0; at < kSource->variants.size(); ++at) {
        RAWFRAME_EXPECT(kPseudo->variants[at].keys == kSource->variants[at].keys);
    }
    RAWFRAME_EXPECT(refusedWith(pseudoLocalize("{$x"), LocalizationError::MessageInvalid));
    // Out of the message's limits once grown.
    RAWFRAME_EXPECT(refusedWith(pseudoLocalize("Play", {}, {.maximumBytes = 8}), LocalizationError::OverLimit));
}

RAWFRAME_TEST(APseudoLocaleServesThroughTheCatalog) {
    const base::Bits128 kHud = base::parseBits128Hex("0123456789abcdef0123456789abcdef").value;
    StringTable table{.sourceLocale = *parseLocale("en"), .entries = {}};
    table.entries["hud.files"] = SourceEntry{.message = std::string{kFiles}, .description = {}};
    table.entries["menu.play"] = SourceEntry{.message = "Play", .description = {}};
    const auto kPseudo = pseudoTranslations(kHud, table, *parseLocale("en-XA"));
    RAWFRAME_EXPECT(kPseudo.has_value() && kPseudo->entries.size() == 2);
    const std::vector<TableDocument> kTables{{.id = kHud, .table = table}};
    const std::vector<Translations> kTranslations{*kPseudo};
    // Only the development door takes a private-use locale.
    RAWFRAME_EXPECT(refusedWith(Catalog::build(kTables, kTranslations), LocalizationError::DocumentInvalid));
    const auto kCatalog = Catalog::buildWithPseudo(kTables, {}, kTranslations);
    RAWFRAME_EXPECT(kCatalog.has_value() && kCatalog->stale().empty());
    const Locale kXa = *parseLocale("en-XA");
    const std::vector<Argument> kCount{{"count", std::int64_t{1234}}};
    RAWFRAME_EXPECT(kCatalog->format(kHud, "menu.play", kXa, kXa, {}) == "[Ṗĺáý~~~~]");
    RAWFRAME_EXPECT(kCatalog->format(kHud, "hud.files", kXa, kXa, kCount) == "[1,234 ƒíĺéš {óķ}~~~~~~~~~]");
    // An authored locale is not a pseudo one.
    const auto kNotPrivate = pseudoTranslations(kHud, table, *parseLocale("tr"));
    const std::vector<Translations> kTurkish{*kNotPrivate};
    RAWFRAME_EXPECT(refusedWith(Catalog::buildWithPseudo(kTables, {}, kTurkish), LocalizationError::DocumentInvalid));
}
