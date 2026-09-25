// String tables and translations (SPEC-0033): each document in its one
// form, keys in their grammar, every message checked, and hostile
// documents refused whole.

#include "rawframe/localization/errors.h"
#include "rawframe/localization/table.h"
#include "rawframe/test/test.h"

#include <string>
#include <string_view>

using namespace rawframe;
using namespace rawframe::localization;

namespace {

bool refusedWith(const auto& outcome, LocalizationError error) {
    return !outcome.has_value() && outcome.error().domain() == kLocalizationDomain &&
           outcome.error().code() == code(error);
}

constexpr std::string_view kTable = R"({
  "formatVersion": 1,
  "kind": "text.strings",
  "sourceLocale": "en",
  "entries": {
    "hud.files": {
      "message": ".input {$count :integer}\n.match $count\none {{One file}}\n* {{{$count} files}}"
    },
    "menu.play": {
      "message": "Play",
      "description": "The main menu's button that starts a match."
    }
  }
}
)";

constexpr std::string_view kTranslations = R"({
  "formatVersion": 1,
  "kind": "text.translations",
  "table": "0123456789abcdef0123456789abcdef",
  "locale": "tr",
  "entries": {
    "menu.play": {
      "message": "Oyna",
      "sourceHash": "3f1c0a2b9d8e7f60"
    }
  }
}
)";

std::string replaced(std::string_view text, std::string_view from, std::string_view to) {
    std::string made{text};
    const std::size_t kAt = made.find(from);
    RAWFRAME_EXPECT(kAt != std::string::npos);
    if (kAt != std::string::npos) {
        made.replace(kAt, from.size(), to);
    }
    return made;
}

} // namespace

RAWFRAME_TEST(ATableReadsAndWritesInItsOneForm) {
    const auto kRead = readStrings(kTable);
    RAWFRAME_EXPECT(kRead.has_value());
    RAWFRAME_EXPECT(kRead->sourceLocale.text() == "en" && kRead->entries.size() == 2 &&
                    kRead->entries.at("menu.play").message == "Play" &&
                    kRead->entries.at("hud.files").description.empty());
    RAWFRAME_EXPECT(writeStrings(*kRead) == std::string{kTable});
    const auto kTranslated = readTranslations(kTranslations);
    RAWFRAME_EXPECT(kTranslated.has_value());
    RAWFRAME_EXPECT(kTranslated->locale.text() == "tr" &&
                    kTranslated->entries.at("menu.play").sourceHash == "3f1c0a2b9d8e7f60");
    RAWFRAME_EXPECT(writeTranslations(*kTranslated) == std::string{kTranslations});
}

RAWFRAME_TEST(KeysAreDotSeparatedMachineSegments) {
    for (const std::string_view kGood : {"a", "menu.play", "hud.ammo_count2", "a.b.c.d.e.f.g.h"}) {
        RAWFRAME_EXPECT(validKey(kGood));
    }
    for (const std::string_view kBad :
         {"", "Menu", "menu.", ".menu", "menu..play", "2d", "menu.2d", "menu-play", "a.b.c.d.e.f.g.h.i", "menu play"}) {
        RAWFRAME_EXPECT(!validKey(kBad));
    }
    RAWFRAME_EXPECT(validKey(std::string(128, 'a')) && !validKey(std::string(129, 'a')));
}

RAWFRAME_TEST(TheSourceHashIsTheMessagesDigest) {
    RAWFRAME_EXPECT(sourceHashOf("") == "e3b0c44298fc1c14" && sourceHashOf("Play") == "436e61016e26fcb7");
    RAWFRAME_EXPECT(sourceHashOf("Play ") != sourceHashOf("Play"));
}

RAWFRAME_TEST(DocumentsOutOfTheirFormAreRefused) {
    for (const auto& [kFrom, kTo] : std::initializer_list<std::pair<std::string_view, std::string_view>>{
             {"\"formatVersion\": 1", "\"formatVersion\": 2"},
             {"text.strings", "text.table"},
             {"\"en\"", "\"EN\""},
             {"\"en\"", "\"iw\""},
             {"\"en\"", "\"qaa\""},
             {"\"en\"", "\"en-US-x-private\""},
             {"hud.files", "hud.Files"},
             {"\"menu.play\"", "\"hud.files\""},
             {"The main menu's button that starts a match.", ""},
             {"\"message\": \"Play\"", "\"message\": 1"},
             {"\"sourceLocale\": \"en\",\n", ""},
             {"  \"formatVersion\": 1,\n  \"kind\": \"text.strings\",",
              "  \"kind\": \"text.strings\",\n  \"formatVersion\": 1,"},
             {"\"hud.files\"", "\"zz.files\""},
             {"\n}\n", "\n}"},
             {"\"description\"", "\"note\""},
         }) {
        const auto kRead = readStrings(replaced(kTable, kFrom, kTo));
        RAWFRAME_EXPECT(refusedWith(kRead, LocalizationError::DocumentInvalid));
    }
    for (const auto& [kFrom, kTo] : std::initializer_list<std::pair<std::string_view, std::string_view>>{
             {"0123456789abcdef0123456789abcdef", "0123456789ABCDEF0123456789abcdef"},
             {"0123456789abcdef0123456789abcdef", "00000000000000000000000000000000"},
             {"3f1c0a2b9d8e7f60", "3f1c0a2b9d8e7f6"},
             {"3f1c0a2b9d8e7f60", "3F1C0A2B9D8E7F60"},
             {"\"tr\"", "\"tr-tr\""},
             {"\"sourceHash\": \"3f1c0a2b9d8e7f60\"", "\"description\": \"x\""},
             {"text.translations", "text.strings"},
         }) {
        const auto kRead = readTranslations(replaced(kTranslations, kFrom, kTo));
        RAWFRAME_EXPECT(refusedWith(kRead, LocalizationError::DocumentInvalid));
    }
}

RAWFRAME_TEST(EveryMessageIsCheckedWithItsKey) {
    const auto kRead = readStrings(replaced(kTable, "\"Play\"", "\"{$x :datetime}\""));
    RAWFRAME_EXPECT(refusedWith(kRead, LocalizationError::MessageInvalid));
    bool named = false;
    for (const auto& field : kRead.error().context()) {
        named = named || (field.key == "key" && field.value == "menu.play");
    }
    RAWFRAME_EXPECT(named);
    RAWFRAME_EXPECT(refusedWith(readTranslations(replaced(kTranslations, "\"Oyna\"", "\" Oyna\"")),
                                LocalizationError::MessageInvalid));
}

RAWFRAME_TEST(DocumentsPastTheirLimitsAreRefused) {
    RAWFRAME_EXPECT(refusedWith(readStrings(kTable, {.maximumKeys = 1}), LocalizationError::OverLimit));
    RAWFRAME_EXPECT(refusedWith(readStrings(kTable, {.maximumDocumentBytes = 64}), LocalizationError::OverLimit));
    RAWFRAME_EXPECT(refusedWith(readStrings(kTable, {.message = {.maximumBytes = 3}}), LocalizationError::OverLimit));
    StringTable table{.sourceLocale = *parseLocale("en"), .entries = {}};
    for (int at = 0; at < 3; ++at) {
        table.entries.emplace("k" + std::to_string(at), SourceEntry{.message = "x", .description = {}});
    }
    RAWFRAME_EXPECT(refusedWith(writeStrings(table, {.maximumKeys = 2}), LocalizationError::OverLimit));
}

RAWFRAME_TEST(HostileDocumentsAreReadOrRefusedWhole) {
    // Every prefix, and every byte made each of JSON's and the grammar's
    // own characters: refused as a document, a message, or a limit, or read
    // to what writes back as the same bytes.
    for (const std::string_view kSeed : {kTable, kTranslations}) {
        std::string text{kSeed};
        const auto kTry = [&text, kSeed]() {
            const bool kStrings = kSeed.find("text.strings") != std::string_view::npos;
            if (kStrings) {
                const auto kRead = readStrings(text);
                RAWFRAME_EXPECT(kRead.has_value() ? writeStrings(*kRead) == text
                                                  : refusedWith(kRead, LocalizationError::DocumentInvalid) ||
                                                        refusedWith(kRead, LocalizationError::MessageInvalid));
            } else {
                const auto kRead = readTranslations(text);
                RAWFRAME_EXPECT(kRead.has_value() ? writeTranslations(*kRead) == text
                                                  : refusedWith(kRead, LocalizationError::DocumentInvalid) ||
                                                        refusedWith(kRead, LocalizationError::MessageInvalid));
            }
        };
        for (std::size_t at = 0; at < kSeed.size(); ++at) {
            text = kSeed.substr(0, at);
            kTry();
        }
        for (std::size_t at = 0; at < kSeed.size(); ++at) {
            text = kSeed;
            for (const char kEach : std::string_view{"{}[]\":,.\\ 0a\n"}) {
                text[at] = kEach;
                kTry();
            }
        }
    }
}
