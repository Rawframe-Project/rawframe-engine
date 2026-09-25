// Locales (SPEC-0033): the tag grammar in canonical case, intake from what
// players and platforms give, likely subtags, and the fallback chain's
// normative proof cases.

#include "rawframe/localization/errors.h"
#include "rawframe/localization/locale.h"
#include "rawframe/test/test.h"

#include <algorithm>
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

Locale tag(std::string_view text) {
    return *parseLocale(text);
}

/// A chain from `requested` to English, then to a Japanese source, as text.
std::vector<std::string> chainOf(std::string_view requested) {
    const auto kChain = fallbackChain(tag(requested), tag("en"), tag("ja"));
    std::vector<std::string> made;
    for (const Locale& each : kChain.has_value() ? *kChain : std::vector<Locale>{}) {
        made.push_back(each.text());
    }
    return made;
}

bool contains(const std::vector<std::string>& chain, std::string_view locale) {
    return std::ranges::contains(chain, std::string{locale});
}

std::size_t place(const std::vector<std::string>& chain, std::string_view locale) {
    return static_cast<std::size_t>(std::ranges::find(chain, std::string{locale}) - chain.begin());
}

} // namespace

RAWFRAME_TEST(ADocumentsTagIsCanonical) {
    for (const std::string_view kGood : {"en", "tr", "zh-Hant-TW", "es-419", "sr-Latn", "pt-BR", "yue"}) {
        const auto kParsed = parseLocale(kGood);
        RAWFRAME_EXPECT(kParsed.has_value() && kParsed->text() == kGood);
    }
    for (const std::string_view kBad :
         {"", "EN", "en-us", "zh-hant", "e", "engl", "en-US-x-private", "ca-ES-valencia", "en--US", "en-", "en_US"}) {
        RAWFRAME_EXPECT(refusedWith(parseLocale(kBad), LocalizationError::LocaleInvalid));
    }
}

RAWFRAME_TEST(IntakeMakesTagsCanonical) {
    const auto kText = [](std::string_view given) {
        const auto kMade = intake(given);
        return kMade.has_value() ? kMade->text() : std::string{};
    };
    RAWFRAME_EXPECT(kText("en_us") == "en-US" && kText("ZH-hant-tw") == "zh-Hant-TW");
    // CLDR's aliases: a deprecated language, a legacy one bringing a
    // script, and an overlong region.
    RAWFRAME_EXPECT(kText("iw") == "he" && kText("in-ID") == "id-ID" && kText("sh") == "sr-Latn");
    RAWFRAME_EXPECT(kText("sh-Cyrl") == "sr-Cyrl");
    // Well formed, but no language CLDR knows; and not a tag at all.
    RAWFRAME_EXPECT(refusedWith(intake("qqq"), LocalizationError::LocaleUnknown));
    RAWFRAME_EXPECT(refusedWith(intake("en-ZZZZZ"), LocalizationError::LocaleInvalid));
    RAWFRAME_EXPECT(refusedWith(intake("en-US.UTF-8"), LocalizationError::LocaleInvalid));
}

RAWFRAME_TEST(LikelySubtagsFillATagIn) {
    RAWFRAME_EXPECT(maximize(tag("zh-TW")).text() == "zh-Hant-TW");
    RAWFRAME_EXPECT(maximize(tag("zh")).text() == "zh-Hans-CN");
    RAWFRAME_EXPECT(maximize(tag("sr")).text() == "sr-Cyrl-RS");
    RAWFRAME_EXPECT(maximize(tag("sr-Latn")).text() == "sr-Latn-RS");
    RAWFRAME_EXPECT(maximize(tag("en")).text() == "en-Latn-US");
    // What CLDR has nothing for stays as it was.
    RAWFRAME_EXPECT(maximize(tag("qqq")).text() == "qqq");
}

RAWFRAME_TEST(TheChainFollowsCldrsParents) {
    // zh-TW is Traditional all the way and never reaches Simplified zh.
    const std::vector<std::string> kTaiwan = chainOf("zh-TW");
    RAWFRAME_EXPECT(kTaiwan.size() >= 3 && kTaiwan[0] == "zh-Hant-TW" && kTaiwan[1] == "zh-TW");
    RAWFRAME_EXPECT(contains(kTaiwan, "zh-Hant") && !contains(kTaiwan, "zh") && !contains(kTaiwan, "zh-Hans"));
    // pt-MO goes by pt-PT; pt-BR never does.
    const std::vector<std::string> kMacao = chainOf("pt-MO");
    RAWFRAME_EXPECT(contains(kMacao, "pt-PT") && place(kMacao, "pt-PT") < place(kMacao, "pt"));
    const std::vector<std::string> kBrazil = chainOf("pt-BR");
    RAWFRAME_EXPECT(contains(kBrazil, "pt-BR") && contains(kBrazil, "pt") && !contains(kBrazil, "pt-PT"));
    // es-419 is a region of its own, and Mexico's parent.
    const std::vector<std::string> kMexico = chainOf("es-MX");
    RAWFRAME_EXPECT(contains(kMexico, "es-419") && place(kMexico, "es-419") < place(kMexico, "es"));
    // English of Australia by the world's English; Serbian in Latin never
    // reaches Cyrillic sr.
    const std::vector<std::string> kAustralia = chainOf("en-AU");
    RAWFRAME_EXPECT(contains(kAustralia, "en-001") && place(kAustralia, "en-001") < place(kAustralia, "en"));
    RAWFRAME_EXPECT(!contains(chainOf("sr-Latn-RS"), "sr"));
    // The project's default, then the source, end every chain, once.
    const std::vector<std::string> kTurkish = chainOf("tr");
    RAWFRAME_EXPECT(kTurkish == (std::vector<std::string>{"tr-Latn-TR", "tr-TR", "tr-Latn", "tr", "en", "ja"}));
    const std::vector<std::string> kEnglish = chainOf("en");
    RAWFRAME_EXPECT(kEnglish.back() == "ja" && place(kEnglish, "en") < place(kEnglish, "ja") &&
                    std::ranges::count(kEnglish, std::string{"en"}) == 1);
    RAWFRAME_EXPECT(refusedWith(fallbackChain(tag("zh-TW"), tag("en"), tag("ja"), {.maximumLocales = 3}),
                                LocalizationError::OverLimit));
}
