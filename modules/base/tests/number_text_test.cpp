// How an authored document spells a number: as nine significant digits
// where they read back exactly, so familiar numbers keep their text, and
// in full where they do not.

#include "rawframe/base/number_text.h"
#include "rawframe/test/test.h"

#include <charconv>
#include <string>

using namespace rawframe;

namespace {

double readBack(const std::string& text) {
    double read = 0;
    static_cast<void>(std::from_chars(text.data(), text.data() + text.size(), read));
    return read;
}

} // namespace

RAWFRAME_TEST(NumbersOfNineDigitsKeepTheirSpelling) {
    RAWFRAME_EXPECT(base::numberText(100000) == "100000");
    RAWFRAME_EXPECT(base::numberText(12000000) == "12000000");
    RAWFRAME_EXPECT(base::numberText(0.0001) == "0.0001");
    RAWFRAME_EXPECT(base::numberText(0.5) == "0.5");
    RAWFRAME_EXPECT(base::numberText(-3) == "-3");
    RAWFRAME_EXPECT(base::numberText(1e12) == "1e+12");
}

RAWFRAME_TEST(NumbersOfMoreDigitsReadBackExactly) {
    for (const double kValue :
         {0.2000000011, 0.30000000000000004, 123456789.25, 8888888888888888888888880.0, 1234.000000001, -0.1 * 3}) {
        RAWFRAME_EXPECT(readBack(base::numberText(kValue)) == kValue);
    }
    RAWFRAME_EXPECT(base::numberText(0.2000000011) == "0.2000000011");
}
