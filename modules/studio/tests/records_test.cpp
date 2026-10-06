// Studio's records (D435, D439): a component found by what an author types,
// a field's text made a value of its kind or refused, an outcome read, and
// a minted identity in the form the session reads.

#include "../src/records.h"
#include "rawframe/test/test.h"

#include <set>
#include <string>

using namespace rawframe::studio;
namespace document = rawframe::document;

namespace {

Catalog sample() {
    Catalog catalog;
    catalog.operations = {"scene.add_component", "scene.set_field"};
    catalog.components = {{"a", "rawframe.physics2d.body"},
                          {"b", "rawframe.physics2d.pose"},
                          {"c", "runners.tile"},
                          {"d", "runners.target"},
                          {"e", "game.tile"}};
    return catalog;
}

} // namespace

RAWFRAME_TEST(AComponentIsFoundByItsNameItsLastPartOrAStartOnlyItHas) {
    const Catalog kCatalog = sample();
    std::string why;
    RAWFRAME_EXPECT(componentNamed(kCatalog, "runners.tile", why)->id == "c");
    RAWFRAME_EXPECT(componentNamed(kCatalog, "body", why)->id == "a");
    RAWFRAME_EXPECT(componentNamed(kCatalog, "runners.ta", why)->id == "d");
    // Two whose last part is `tile`; the whole name tells them apart.
    RAWFRAME_EXPECT(!componentNamed(kCatalog, "tile", why).has_value());
    RAWFRAME_EXPECT(why == "2 components match tile");
    RAWFRAME_EXPECT(componentNamed(kCatalog, "game.tile", why)->id == "e");
    RAWFRAME_EXPECT(!componentNamed(kCatalog, "rawframe", why).has_value());
    RAWFRAME_EXPECT(!componentNamed(kCatalog, "wheel", why).has_value());
    RAWFRAME_EXPECT(why == "no component matches wheel");
    RAWFRAME_EXPECT(!componentNamed(kCatalog, "", why).has_value());
    RAWFRAME_EXPECT(kCatalog.offers("scene.set_field") && !kCatalog.offers("scene.destroy_entity"));
}

RAWFRAME_TEST(TypedTextIsAValueOfItsFieldsKindOrNone) {
    RAWFRAME_EXPECT(document::writeCompact(*typedValue("real", "2.5")) == R"({"real":2.5})");
    RAWFRAME_EXPECT(!typedValue("real", "x").has_value());
    RAWFRAME_EXPECT(!typedValue("real", "\"2\"").has_value());
    RAWFRAME_EXPECT(document::writeCompact(*typedValue("unsigned", "7")) == R"({"unsigned":"7"})");
    RAWFRAME_EXPECT(document::writeCompact(*typedValue("truth", "true")) == R"({"truth":true})");
    RAWFRAME_EXPECT(!typedValue("truth", "yes").has_value());
    RAWFRAME_EXPECT(!typedValue("reference", "x").has_value());
}

RAWFRAME_TEST(AnOutcomeSaysWhetherItsSlotWasDoneAndWhatCanBeUndone) {
    const Outcome kDone =
        outcomeOf(R"({"kind":"authoring.reply","id":2,"answer":{"undoable":3,"redoable":1,"results":[{"deltas":1}]}})");
    RAWFRAME_EXPECT(kDone.done && kDone.undoable == 3 && kDone.redoable == 1);
    const Outcome kRefused = outcomeOf(
        R"({"kind":"authoring.reply","id":3,"answer":{"undoable":0,"redoable":0,"results":[{"error":{"message":"there is nothing to undo"}}]}})");
    RAWFRAME_EXPECT(!kRefused.done && kRefused.message == "there is nothing to undo");
    const Outcome kInvalid = outcomeOf(R"({"kind":"authoring.reply","id":4,"error":{"message":"bad inputs"}})");
    RAWFRAME_EXPECT(!kInvalid.done && kInvalid.message == "bad inputs");
    RAWFRAME_EXPECT(!outcomeOf("not json").done);
}

RAWFRAME_TEST(AMintedIdentityIsAVersionFourUuidAndNotRepeated) {
    std::set<std::string> minted;
    for (int each = 0; each < 64; ++each) {
        const std::string kIdentity = mintedIdentity();
        RAWFRAME_EXPECT(kIdentity.size() == 36);
        RAWFRAME_EXPECT(kIdentity[8] == '-' && kIdentity[13] == '-' && kIdentity[18] == '-' && kIdentity[23] == '-');
        RAWFRAME_EXPECT(kIdentity[14] == '4');
        RAWFRAME_EXPECT(std::string{"89ab"}.find(kIdentity[19]) != std::string::npos);
        minted.insert(kIdentity);
    }
    RAWFRAME_EXPECT(minted.size() == 64);
}
