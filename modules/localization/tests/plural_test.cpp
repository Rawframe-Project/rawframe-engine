// Plural categories (SPEC-0033): operands from written numbers, and the
// compiled CLDR rules held against every example CLDR gives.

#include "plural_samples.h"
#include "rawframe/localization/plural.h"
#include "rawframe/test/test.h"

#include <cstdio>
#include <string>

using namespace rawframe;
using namespace rawframe::localization;

RAWFRAME_TEST(OperandsAreTheNumberAsWritten) {
    RAWFRAME_EXPECT(operandsOf("-12.340") == (PluralOperands{.i = 12, .v = 3, .f = 340, .w = 2, .t = 34}));
    RAWFRAME_EXPECT(operandsOf("1") == (PluralOperands{.i = 1}));
    RAWFRAME_EXPECT(operandsOf("1.0") == (PluralOperands{.i = 1, .v = 1}));
    RAWFRAME_EXPECT(operandsOf("0.05") == (PluralOperands{.i = 0, .v = 2, .f = 5, .w = 2, .t = 5}));
    for (const std::string_view kBad : {"", "-", "1.", ".5", "1e3", "1,000", "+1", "1234567890123456789", "1.x"}) {
        RAWFRAME_EXPECT(!operandsOf(kBad).has_value());
    }
}

RAWFRAME_TEST(EveryCldrExampleFallsInItsCategory) {
    std::size_t checked = 0;
    for (const oracle::PluralSample& sample : oracle::pluralSamples()) {
        const auto kLocale = parseLocale(sample.locale);
        const auto kOperands = operandsOf(sample.number);
        if (sample.locale == "root") {
            continue;
        }
        RAWFRAME_EXPECT(kLocale.has_value() && kOperands.has_value());
        if (!kLocale.has_value() || !kOperands.has_value()) {
            continue;
        }
        const PluralCategory kMade = sample.ordinal ? ordinal(*kLocale, *kOperands) : cardinal(*kLocale, *kOperands);
        if (kMade != sample.category) {
            std::fprintf(stderr,
                         "%s %s %s: %s, CLDR says %s\n",
                         std::string{sample.locale}.c_str(),
                         sample.ordinal ? "ordinal" : "cardinal",
                         std::string{sample.number}.c_str(),
                         std::string{nameOf(kMade)}.c_str(),
                         std::string{nameOf(sample.category)}.c_str());
        }
        RAWFRAME_EXPECT(kMade == sample.category);
        ++checked;
    }
    RAWFRAME_EXPECT(checked > 5000);
}

RAWFRAME_TEST(ALocaleUsesItsTagsRulesThenItsLanguages) {
    const auto kIn = [](std::string_view tag, std::string_view number) {
        return cardinal(*parseLocale(tag), *operandsOf(number));
    };
    // English: 1 is one, 1.0 is not; Polish few and many; Arabic's six.
    RAWFRAME_EXPECT(kIn("en", "1") == PluralCategory::One && kIn("en", "1.0") == PluralCategory::Other);
    RAWFRAME_EXPECT(kIn("en-GB", "1") == PluralCategory::One);
    RAWFRAME_EXPECT(kIn("pl", "3") == PluralCategory::Few && kIn("pl", "5") == PluralCategory::Many);
    RAWFRAME_EXPECT(kIn("ar", "0") == PluralCategory::Zero && kIn("ar", "2") == PluralCategory::Two &&
                    kIn("ar", "11") == PluralCategory::Many);
    // pt-PT has rules of its own: Brazil's Portuguese counts 0 as one,
    // Portugal's does not.
    RAWFRAME_EXPECT(kIn("pt", "0") == PluralCategory::One && kIn("pt-PT", "0") == PluralCategory::Other);
    // English ordinals, and a language CLDR has no rules for.
    RAWFRAME_EXPECT(ordinal(*parseLocale("en"), *operandsOf("22")) == PluralCategory::Two &&
                    ordinal(*parseLocale("en"), *operandsOf("13")) == PluralCategory::Other);
    RAWFRAME_EXPECT(kIn("qqq", "1") == PluralCategory::Other);
    RAWFRAME_EXPECT(pluralCategory("few") == PluralCategory::Few && !pluralCategory("some").has_value());
}
