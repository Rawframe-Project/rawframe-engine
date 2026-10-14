// A field's typed control (D784): a labelled number shown by its label and
// typed back as its number, a color as #RRGGBBAA, a range said and a value
// past it told, and the values a field is chosen from.

#include "../src/hints.h"
#include "rawframe/test/test.h"

using namespace rawframe::studio;

namespace {

Catalog::Field motion() {
    return Catalog::Field{
        .name = "motion", .kind = "unsigned", .labels = {{0, "static"}, {1, "kinematic"}, {2, "dynamic"}}};
}

} // namespace

RAWFRAME_TEST(ALabelledNumberIsShownAndTypedByItsLabel) {
    RAWFRAME_EXPECT(shownAs(motion(), "2") == "dynamic");
    RAWFRAME_EXPECT(shownAs(motion(), "7") == "7");
    RAWFRAME_EXPECT(typedAs(motion(), "kinematic") == "1");
    RAWFRAME_EXPECT(typedAs(motion(), "2") == "2");
    RAWFRAME_EXPECT((choicesOf(motion()) == std::vector<std::string>{"static", "kinematic", "dynamic"}));
    RAWFRAME_EXPECT((choicesOf(Catalog::Field{.kind = "truth"}) == std::vector<std::string>{"true", "false"}));
    RAWFRAME_EXPECT(choicesOf(Catalog::Field{.kind = "real"}).empty());
}

RAWFRAME_TEST(AColorIsShownAndTypedAsItsHexDigits) {
    const Catalog::Field kColor{.name = "color", .kind = "unsigned", .color = true};
    RAWFRAME_EXPECT(shownAs(kColor, "4294967295") == "#FFFFFFFF");
    RAWFRAME_EXPECT(typedAs(kColor, "#ff0000") == "4278190335");
    RAWFRAME_EXPECT(typedAs(kColor, "#11223344") == std::to_string(0x11223344U));
    RAWFRAME_EXPECT(!colorOf("#ff00").has_value() && !colorOf("ff0000").has_value() && !colorOf("#gg0000").has_value());
}

RAWFRAME_TEST(ARangeIsSaidAndAValuePastItTold) {
    const Catalog::Field kSide{.name = "side", .kind = "real", .least = 4.0, .most = 100.0, .unit = "m"};
    RAWFRAME_EXPECT(hintText(kSide) == "m, 4 to 100");
    RAWFRAME_EXPECT(hintText(Catalog::Field{.least = 0.0}) == "from 0");
    RAWFRAME_EXPECT(hintText(Catalog::Field{}).empty());
    RAWFRAME_EXPECT(pastRange(kSide, "101") && pastRange(kSide, "3.5") && !pastRange(kSide, "20"));
    RAWFRAME_EXPECT(!pastRange(kSide, "wide"));
}
