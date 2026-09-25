// Numbers (SPEC-0033): plain forms rounded the same everywhere, and CLDR's
// symbols, digits, and grouping for each locale.

#include "rawframe/localization/number.h"
#include "rawframe/test/test.h"

#include <cmath>
#include <limits>
#include <string>

using namespace rawframe;
using namespace rawframe::localization;

namespace {

std::string in(std::string_view tag, std::string_view plain, bool grouping = true) {
    return localized(*parseLocale(tag), plain, grouping);
}

} // namespace

RAWFRAME_TEST(PlainNumbersAreRoundedTheSameEverywhere) {
    RAWFRAME_EXPECT(plainInteger(-1234) == "-1234" && plainInteger(0) == "0");
    RAWFRAME_EXPECT(plainDecimal(1.5, 0, 3) == "1.5" && plainDecimal(1.0, 1, 3) == "1.0" &&
                    plainDecimal(1.0, 0, 3) == "1");
    // The binary value rounds to nearest, ties to even: 0.125 is exact and
    // goes to 0.12; 2.675 is a little under and goes to 2.67.
    RAWFRAME_EXPECT(plainDecimal(0.125, 0, 2) == "0.12" && plainDecimal(0.375, 0, 2) == "0.38");
    RAWFRAME_EXPECT(plainDecimal(2.675, 0, 2) == "2.67" && plainDecimal(1.0 / 3.0, 2, 4) == "0.3333");
    RAWFRAME_EXPECT(plainDecimal(-0.0004, 0, 3) == "0" && plainDecimal(-0.0, 1, 1) == "0.0");
    RAWFRAME_EXPECT(plainDecimal(1e20, 0, 0) == "100000000000000000000");
    RAWFRAME_EXPECT(!plainDecimal(std::numeric_limits<double>::infinity(), 0, 3).has_value() &&
                    !plainDecimal(std::nan(""), 0, 3).has_value() && !plainDecimal(1.0, 3, 2).has_value() &&
                    !plainDecimal(1.0, 0, 16).has_value());
    RAWFRAME_EXPECT(plainDecimal(std::numeric_limits<double>::max(), 0, 15).has_value());
}

RAWFRAME_TEST(ALocaleWritesItsOwnSymbolsAndGroups) {
    RAWFRAME_EXPECT(in("en", "-1234567.891") == "-1,234,567.891");
    RAWFRAME_EXPECT(in("de", "1234567.891") == "1.234.567,891" && in("tr", "1234.5") == "1.234,5");
    RAWFRAME_EXPECT(in("fr", "1234567") == "1 234 567" && in("de-CH", "1234567") == "1'234'567");
    // Spanish groups only past four digits; Hindi groups by two past the
    // first three.
    RAWFRAME_EXPECT(in("es", "1234") == "1234" && in("es", "12345") == "12.345");
    RAWFRAME_EXPECT(in("hi", "1234567") == "12,34,567" && in("en-IN", "123456789") == "12,34,56,789");
    // Bengali's own digits; Arabic's marked minus.
    RAWFRAME_EXPECT(in("bn", "123") == "১২৩" && in("ar", "-5") == "‎-5");
    // Without grouping; a region without data of its own takes its
    // language's; a language without any takes the root's.
    RAWFRAME_EXPECT(in("en", "1234567", false) == "1234567" && in("de-BE", "1234.5") == "1.234,5");
    RAWFRAME_EXPECT(in("qqq", "-1234.5") == "-1,234.5");
}
